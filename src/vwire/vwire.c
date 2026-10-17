#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
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
#define VW_HEARTBEAT_INTERVAL_SECS 2
#define VW_VSWITCH_MAX_INACTIVE_SECS 10

typedef struct {
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;
    time_t last_seen;
    pthread_mutex_t lock;
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

    // TODO: remove this after removing all sendto & recvfrom
    vwire->vsw_addr.sin_family = AF_INET;
    vwire->vsw_addr.sin_addr = vsw_addr;
    vwire->vsw_addr.sin_port = htons(VSW_UNDERLAY_PORT);

    vwire->last_seen = time(NULL);
    pthread_mutex_init(&vwire->lock, NULL);

    return 0;

error_close_tap:
    ret = close(vwire->tapfd);
    if (ret == -1)
        log_sys_error();
error:
    return -1;
}

// This function assumes that a recv timeout is set on the socket
int vw_init_conn(int sockfd)
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
        ret = vw_init_conn(sockfd);
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

/*
This function runs on a dedicated thread and is responsible for maintaining heartbeats with the vswitch.
* Continously sends heartbeats messages to the vswitch to let it know the client is still alive.
* Checks, at every iteration, that the time since the last vswitch heartbeat has not exceededa a threshold,
  and if so, terminates the program.
*/
void *vw_send_heartbeats(void *_vwire)
{
    vwire_t *vwire = (vwire_t *)_vwire;

    uint8_t msg = VSW_CTRL_MSG_HEARTBEAT;
    ssize_t bytes_sent;
    int age;
    int sockfd;
    struct sockaddr_in vsw_addr;

    pthread_mutex_lock(&vwire->lock);
    sockfd = vwire->sockfd;
    vsw_addr = vwire->vsw_addr;
    pthread_mutex_unlock(&vwire->lock);

    // Send heartbeats in an infinite loop
    while (true) {
        // Check liveness of the vswitch connection
        pthread_mutex_lock(&vwire->lock);
        age = time(NULL) - vwire->last_seen;
        if (age > VW_VSWITCH_MAX_INACTIVE_SECS) {
            log_message("Connection to vswitch is no longer active, terminating.", LOG_INFO);
            exit(EXIT_SUCCESS);
        }
        pthread_mutex_unlock(&vwire->lock);

        bytes_sent = sendto(
            sockfd,
            &msg,
            sizeof(msg),
            0,
            (struct sockaddr *)&vsw_addr,
            sizeof(vsw_addr)
        );
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }

        // Sleep until the next heartbeat
        sleep(VW_HEARTBEAT_INTERVAL_SECS);
    }
}

/*
Continously reads incoming data from the TAP interface and when it arrives,
forwards it through the underlay UDP socket
*/
void *vw_send_data_to_vswitch(void *_vwire) {
    vwire_t *vwire = (vwire_t *)_vwire;

    // Buffer for storing read ethernet frames + extra bytes for custom tunnel encap
    uint8_t buf[ETHER_MAX_LEN + VSW_CTRL_HDR_SIZE];
    ssize_t bytes_read;
    ssize_t bytes_sent;

    // Shared attributes
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;

    pthread_mutex_lock(&vwire->lock);
    tapfd = vwire->tapfd;
    sockfd = vwire->sockfd;
    vsw_addr = vwire->vsw_addr;
    pthread_mutex_unlock(&vwire->lock);

    // Set message type to regular traffic
    buf[0] = VSW_CTRL_MSG_TRAFFIC;

    while (true) {
        // Read an ethernet frame from the TAP interface
        bytes_read = read(
            tapfd,
            buf + VSW_CTRL_HDR_SIZE,
            sizeof(buf) - VSW_CTRL_HDR_SIZE
        );
        if (bytes_read == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        else if (bytes_read <= ETHER_ADDR_LEN) {
            continue;
        }

        // Forward the frame through the socket
        bytes_sent = sendto(
            sockfd,
            buf,
            bytes_read + VSW_CTRL_HDR_SIZE,
            0,
            (struct sockaddr *)&vsw_addr,
            sizeof(vsw_addr)
        );
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
    }
}

/*
Continously reads incoming data from the underlay socket.
* Controlplane data is handeled appropriately
* Dataplane date is forwarded via the tap to the linux kernel networking stack
*/
void *vw_recv_data_from_vswitch(void *_vwire) {
    vwire_t *vwire = (vwire_t *)_vwire;

    // Buffer for storing received ethernet frames
    uint8_t buf[ETHER_MAX_LEN + VSW_CTRL_HDR_SIZE];
    uint8_t frame_type;
    uint8_t *ether_frame;
    size_t ether_frame_size;
    ssize_t bytes_written;
    ssize_t bytes_recvd;
    socklen_t vsw_addr_size;

    // Shared attributes
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;

    pthread_mutex_lock(&vwire->lock);
    tapfd = vwire->tapfd;
    sockfd = vwire->sockfd;
    vsw_addr = vwire->vsw_addr;
    pthread_mutex_unlock(&vwire->lock);

    while (true) {
        // Receive a frame from the socket
        bytes_recvd = recvfrom(
            sockfd,
            buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&vsw_addr,
            &vsw_addr_size
        );
        if (bytes_recvd == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }

        if (bytes_recvd < VSW_CTRL_HDR_SIZE) {
            continue;
        }
        ether_frame = buf + VSW_CTRL_HDR_SIZE;
        ether_frame_size = bytes_recvd - VSW_CTRL_HDR_SIZE;
        frame_type = *buf;

        switch (frame_type)
        {
            case VSW_CTRL_MSG_TRAFFIC:
                // Dataplane frame received - write it to the TAP
                if (ether_frame_size <= ETHER_HDR_LEN) {
                    continue;
                }
                bytes_written = write(
                    tapfd,
                    ether_frame,
                    ether_frame_size
                );
                if (bytes_written == -1) {
                    log_sys_error();
                    exit(EXIT_FAILURE);
                }
                break;

            case VSW_CTRL_MSG_HEARTBEAT:
                pthread_mutex_lock(&vwire->lock);
                // vswitch heartbeat received, reset the last seen parameter
                vwire->last_seen = time(NULL);
                pthread_mutex_unlock(&vwire->lock);
                break;

            default:
                break;
        }
    }
}

int main(int argc, char **argv)
{
    vwire_t vwire = {0};
    struct in_addr vsw_addr;
    int ret;

    pthread_t send_thread_pid;
    pthread_t recv_thread_pid;
    pthread_t heartbeat_thread_pid;

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

    ret = vw_conn_vswitch(vwire.sockfd);
    switch (ret)
    {
        case 0:
            log_message("Successfully connected to the vswitch", LOG_INFO);
            break;
        case -1:
            return EXIT_FAILURE;
        case VW_ERR_CONN_TIMEOUT:
            log_message("Connection to vswitch timed out", LOG_ERROR);
            return EXIT_FAILURE;
        case VW_ERR_VSWITCH_FULL:
            log_message("Cannot connect to vswitch since it is full", LOG_ERROR);
            return EXIT_FAILURE;
        case VW_ERR_PROTO:
            log_message("Protocol error encountered while connecting to vswitch", LOG_CRITICAL);
            return EXIT_FAILURE;
        default:
            assert(false);
    }

    // Start two threads - one for receiving and one for sending
    pthread_create(&send_thread_pid, NULL, &vw_send_data_to_vswitch, &vwire);
    pthread_create(&recv_thread_pid, NULL, &vw_recv_data_from_vswitch, &vwire);
    pthread_create(&heartbeat_thread_pid, NULL, &vw_send_heartbeats, &vwire);

    pthread_join(send_thread_pid, NULL);
    pthread_join(recv_thread_pid, NULL);
    pthread_join(heartbeat_thread_pid, NULL);

    return EXIT_SUCCESS;
}
