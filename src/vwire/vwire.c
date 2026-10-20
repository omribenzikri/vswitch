#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include "util/logging.h"
#include "proto.h"

#define VW_CONN_MAX_RETRIES 5
#define VW_CONN_TIMEOUT_SECS 2
#define VW_KEEPALIVE_INTERVAL_SECS 2
#define VW_VSWITCH_MAX_INACTIVE_SECS 10

typedef struct {
    int tapfd;
    int sockfd;
    _Atomic time_t vsw_last_seen;
} vwire_t;

typedef enum {
    VW_ERR_CONN_TIMEOUT = 1,
    VW_ERR_VSWITCH_FULL = 2,
    VW_ERR_PROTO        = 3
} vwire_err_t;


int vw_tap_alloc(const char *name)
{
    struct ifreq ifr = {0};
    int tapfd;
    int ret;

    tapfd = open("/dev/net/tun", O_RDWR);
    if (tapfd == -1) {
        log_sys_error();
        goto error;
    }

    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);

    ret = ioctl(tapfd, TUNSETIFF, &ifr);
    if (ret == -1) {
        log_sys_error();
        goto error_close_tap;
    }

    return tapfd;

error_close_tap:
    ret = close(tapfd);
    if (ret == -1)
        log_sys_error();
error:
    return -1;
}

int vw_sock_alloc(struct in_addr vsw_addr)
{
    struct sockaddr_in vsw_sockaddr = {0};
    int sockfd;
    int ret;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        log_sys_error();
        goto error;
    }

    vsw_sockaddr.sin_family = AF_INET;
    vsw_sockaddr.sin_addr = vsw_addr;
    vsw_sockaddr.sin_port = htons(VSW_UNDERLAY_PORT);

    ret = connect(sockfd, (struct sockaddr *)&vsw_sockaddr,
                  sizeof(vsw_sockaddr));
    if (ret == -1) {
        log_sys_error();
        goto error_close_sock;
    }

    return sockfd;

error_close_sock:
    ret = close(sockfd);
    if (ret == -1)
        log_sys_error();
error:
    return -1;
}

int vw_alloc_resources(vwire_t *vwire, struct in_addr vsw_addr, const char *tap_name)
{
    int ret;

    vwire->tapfd = vw_tap_alloc(tap_name);
    if (vwire->tapfd == -1)
        goto error;

    log_message("Successfully opened TAP interface", LOG_INFO);

    vwire->sockfd = vw_sock_alloc(vsw_addr);
    if (vwire->sockfd == -1)
        goto error_close_tap;

    return 0;

error_close_tap:
    ret = close(vwire->tapfd);
    if (ret == -1)
        log_sys_error();
error:
    return -1;
}

// This function assumes that a recv timeout is set on the socket
int vw_conn_init(int sockfd)
{
    uint8_t req = VSW_CTRL_MSG_JOIN;
    uint8_t res[VSW_CTRL_HDR_SIZE];
    ssize_t bytes_recvd;
    ssize_t bytes_sent;

    bytes_sent = send(sockfd, &req, sizeof(req), 0);
    if (bytes_sent == -1) {
        log_sys_error();
        return -1;
    }

    bytes_recvd = recv(sockfd, res, sizeof(res), 0);
    if (bytes_recvd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return VW_ERR_CONN_TIMEOUT;

        log_sys_error();
        return -1;
    }

    switch (*res) {
        case VSW_CTRL_MSG_FREE:
            return 0;
        case VSW_CTRL_MSG_FULL:
            return VW_ERR_VSWITCH_FULL;
        default:
            return VW_ERR_PROTO;
    }
}

int vw_conn_vswitch(int sockfd)
{
    int ret = 0;
    int err;

    const struct timeval timeout_tv = {
        .tv_sec = VW_CONN_TIMEOUT_SECS,
        .tv_usec = 0
    };
    const struct timeval default_timeout_tv = {
        .tv_sec = 0,
        .tv_usec = 0
    };

    err = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                     &timeout_tv, sizeof(timeout_tv));
    if (err) {
        log_sys_error();
        return -1;
    }

    for (int i = 0; i < VW_CONN_MAX_RETRIES; i++) {
        ret = vw_conn_init(sockfd);
        if (ret != VW_ERR_CONN_TIMEOUT)
            break;
    }

    err = setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO,
                     &default_timeout_tv, sizeof(default_timeout_tv));
    if (err) {
        log_sys_error();
        return -1;
    }

    return ret;
}

int vw_send_heartbeat(int sockfd)
{
    uint8_t msg = VSW_CTRL_MSG_HEARTBEAT;
    ssize_t bytes_sent;

    bytes_sent = send(sockfd, &msg, sizeof(msg), 0);
    if (bytes_sent == -1) {
        log_sys_error();
        return -1;
    }

    return 0;
}

void *vw_conn_keepalive(void *_vwire)
{
    vwire_t *vwire = (vwire_t *)_vwire;
    int age;
    int ret;

    while (true) {
        age = time(NULL) - atomic_load(&vwire->vsw_last_seen);
        if (age > VW_VSWITCH_MAX_INACTIVE_SECS) {
            log_message("Connection to vswitch is no longer active, terminating", LOG_WARNING);
            exit(EXIT_FAILURE);
        }

        ret = vw_send_heartbeat(vwire->sockfd);
        if (ret == -1)
            exit(EXIT_FAILURE);


        sleep(VW_KEEPALIVE_INTERVAL_SECS);
    }
}

int vw_handle_incoming_packet(vwire_t *vwire, uint8_t *payload, size_t size)
{
    uint8_t *ether_frame;
    size_t ether_frame_size;
    ssize_t bytes_written;

    switch (*payload)
    {
        case VSW_CTRL_MSG_TRAFFIC:
            ether_frame = payload + VSW_CTRL_HDR_SIZE;
            ether_frame_size = size - VSW_CTRL_HDR_SIZE;
            if (ether_frame_size < ETHER_HDR_LEN)
                return VW_ERR_PROTO;

            bytes_written = write(vwire->tapfd, ether_frame, ether_frame_size);
            if (bytes_written == -1) {
                log_sys_error();
                return -1;
            }
            break;

        case VSW_CTRL_MSG_HEARTBEAT:
            atomic_store(&vwire->vsw_last_seen, time(NULL));
            break;

        default:
            return VW_ERR_PROTO;
    }

    return 0;
}

void *vw_send_packets_to_vswitch(void *_vwire)
{
    vwire_t *vwire = (vwire_t *)_vwire;
    uint8_t payload[VSW_CTRL_HDR_SIZE + ETHER_MAX_LEN];
    ssize_t bytes_read;
    ssize_t bytes_sent;

    payload[0] = VSW_CTRL_MSG_TRAFFIC;

    while (true) {
        bytes_read = read(vwire->tapfd, payload + VSW_CTRL_HDR_SIZE, ETHER_MAX_LEN);
        if (bytes_read == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        else if (bytes_read < ETHER_HDR_LEN) {
            log_message("Protocol error encountered while handling outgoing packet", LOG_ERROR);
            continue;
        }

        bytes_sent = send(vwire->sockfd, payload, VSW_CTRL_HDR_SIZE + bytes_read, 0);
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
    }
}

void *vw_recv_packets_from_vswitch(void *_vwire)
{
    vwire_t *vwire = (vwire_t *)_vwire;
    uint8_t payload[VSW_CTRL_HDR_SIZE + ETHER_MAX_LEN];
    ssize_t bytes_recvd;
    int ret;

    while (true) {
        bytes_recvd = recv(vwire->sockfd, payload, sizeof(payload), 0);
        if (bytes_recvd == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }

        ret = vw_handle_incoming_packet(vwire, payload, bytes_recvd);
        if (ret == -1)
            exit(EXIT_FAILURE);
        if (ret == VW_ERR_PROTO) {
            log_message("Protocol error encountered while handling incoming packet", LOG_ERROR);
            continue;
        }
    }
}

int vw_start(vwire_t *vwire)
{
    pthread_t send_thread_pid;
    pthread_t recv_thread_pid;
    pthread_t keepalive_thread_pid;
    int ret;

    ret = vw_conn_vswitch(vwire->sockfd);
    switch (ret)
    {
        case 0:
            log_message("Successfully connected to the vswitch", LOG_INFO);
            break;
        case -1:
            return -1;
        case VW_ERR_CONN_TIMEOUT:
            log_message("Connection to vswitch timed out", LOG_ERROR);
            return -1;
        case VW_ERR_VSWITCH_FULL:
            log_message("Cannot connect to vswitch since it has no available interfaces", LOG_ERROR);
            return -1;
        case VW_ERR_PROTO:
            log_message("Protocol error encountered while connecting to vswitch", LOG_CRITICAL);
            return -1;
        default:
            assert(false);
    }

    atomic_store(&vwire->vsw_last_seen, time(NULL));

    ret = pthread_create(&send_thread_pid, NULL, &vw_send_packets_to_vswitch, vwire)   ||
          pthread_create(&recv_thread_pid, NULL, &vw_recv_packets_from_vswitch, vwire) ||
          pthread_create(&keepalive_thread_pid, NULL, &vw_conn_keepalive, vwire);

    if (ret != 0) {
        log_message("Failed to start essential threads", LOG_CRITICAL);
        return -1;
    }

    ret = pthread_join(send_thread_pid, NULL) ||
          pthread_join(recv_thread_pid, NULL) ||
          pthread_join(keepalive_thread_pid, NULL);
    if (ret != 0) {
        log_message("Failed to join with essential threads", LOG_CRITICAL);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    vwire_t vwire = {0};
    struct in_addr vsw_addr;
    int ret;

    if (argc != 3) {
        fprintf(stderr, "Usage: vwire ADDR TAP\n");
        return EXIT_FAILURE;
    }

    ret = inet_pton(AF_INET, argv[1], &vsw_addr);
    if (ret != 1) {
        fprintf(stderr, "Error: invalid vswitch address supplied\n");
        return EXIT_FAILURE;
    }

    ret = vw_alloc_resources(&vwire, vsw_addr, argv[2]);
    if (ret == -1)
        return EXIT_FAILURE;

    ret = vw_start(&vwire);
    if (ret == -1)
        return EXIT_FAILURE;

    log_message("Gracefully exiting", LOG_INFO);

    return EXIT_SUCCESS;
}
