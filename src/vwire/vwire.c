#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <stdbool.h>
#include <pthread.h>
#include "util/logging.h"
#include "proto.h"

#define VW_HEARTBEAT_INTERVAL_SECS 2
#define VW_JOIN_MAX_RETRIES 5
#define VW_RECV_TIMEOUT_SECS 2
#define VW_VSWITCH_UNREACHABLE -1

// Represents a virtual link to the overlay network
typedef struct {
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;
} vwire_t;

/*
Obtain ownership and return a file descriptor for TAP interface
*/
int vw_tap_alloc(char *name) {
    const int tapfd = open("/dev/net/tun", O_RDWR);
    if (tapfd == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    // Set up a request for our TAP
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", name);

    // Take ownership of the TAP
    int ret = ioctl(tapfd, TUNSETIFF, &ifr);
    if (ret == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    return tapfd;
}

/*
Attemps to connect to the vswitch by continously sending join requests
until a response is received from the vswitch. If a response was received, it is returned,
otherwise, -1 returned.
*/
int vw_conn_vswitch(const vwire_t *vwire) {
    uint8_t req = VSW_CTRL_MSG_JOIN;
    uint8_t res;
    ssize_t bytes_recvd;
    ssize_t bytes_sent;
    socklen_t vsw_addr_size;
    int ret;

    struct timeval timeout_tv = {
        .tv_sec = VW_RECV_TIMEOUT_SECS,
        .tv_usec = 0
    };
    struct timeval default_tv = {
        .tv_sec = 0,
        .tv_usec = 0
    };
    
    // Set a timeout for the recv call
    ret = setsockopt(vwire->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout_tv, sizeof(timeout_tv));
    if (ret == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < VW_JOIN_MAX_RETRIES; i++) {
        // Send a join request to the vswitch
        bytes_sent = sendto(
            vwire->sockfd,
            &req,
            sizeof(req),
            0,
            (struct sockaddr *)&vwire->vsw_addr,
            sizeof(vwire->vsw_addr)
        );
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        // Wait for a response from the vswitch
        bytes_recvd = recvfrom(
            vwire->sockfd,
            &res,
            sizeof(res),
            0,
            (struct sockaddr *)&vwire->vsw_addr,
            &vsw_addr_size
        );
        if (bytes_recvd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        // Response received from vswitch
        else if (bytes_recvd > 0) {
            // Set timeout to the default
            ret = setsockopt(vwire->sockfd, SOL_SOCKET, SO_RCVTIMEO, &default_tv, sizeof(default_tv));
            if (ret == -1) {
                log_sys_error();
                exit(EXIT_FAILURE);
            }
            return res;
        }
    }

    // No response received from vswitch, set timeout to the default
    ret = setsockopt(vwire->sockfd, SOL_SOCKET, SO_RCVTIMEO, &default_tv, sizeof(default_tv));
    if (ret == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
    return VW_VSWITCH_UNREACHABLE;
}

/*
Continously sends heartbeats messages to the vswitch to let it know the client is still alive.
*/
void *vw_send_heartbeats(void *_vwire) {
    const vwire_t *vwire = (const vwire_t *)_vwire;
    uint8_t msg = VSW_CTRL_MSG_HEARTBEAT;
    ssize_t bytes_sent;

    // Send heartbeats in an infinite loop
    while (true) {
        bytes_sent = sendto(
            vwire->sockfd,
            &msg,
            sizeof(msg),
            0,
            (struct sockaddr *)&vwire->vsw_addr,
            sizeof(vwire->vsw_addr)
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
    const vwire_t *vwire = (const vwire_t *)_vwire;

    // Buffer for storing read ethernet frames + extra bytes for custom tunnel encap
    uint8_t buf[ETHER_MAX_LEN + VSW_CTRL_HDR_SIZE];
    ssize_t ether_bytes_read;

    // Set message type to regular traffic
    buf[0] = VSW_CTRL_MSG_TRAFFIC;
    ssize_t bytes_sent;

    while (true) {
        // Read an ethernet frame from the TAP interface
        ether_bytes_read = read(
            vwire->tapfd,
            buf + VSW_CTRL_HDR_SIZE,
            sizeof(buf) - VSW_CTRL_HDR_SIZE
        );
        if (ether_bytes_read == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        else if (ether_bytes_read <= ETHER_ADDR_LEN) {
            continue;
        }
    
        // Forward the frame through the socket
        bytes_sent = sendto(
            vwire->sockfd,
            buf,
            ether_bytes_read + VSW_CTRL_HDR_SIZE,
            0,
            (struct sockaddr *)&vwire->vsw_addr,
            sizeof(vwire->vsw_addr)
        );
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
    }
}

/*
Continously reads incoming data from the underlay socket.
* Control-plane data is handeled appropriately
* Data-plane date is forwarded via the tap to the linux kernel networking stack
*/
void *vw_recv_data_from_vswitch(void *_vwire) {
    const vwire_t *vwire = (const vwire_t *)_vwire;

    // Buffer for storing received ethernet frames
    uint8_t buf[ETHER_MAX_LEN + VSW_CTRL_HDR_SIZE];
    ssize_t ether_bytes_written;
    ssize_t bytes_recvd;
    socklen_t vsw_addr_size;

    while (true) {
        // Receive a frame from the socket
        bytes_recvd = recvfrom(
            vwire->sockfd,
            buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&vwire->vsw_addr,
            &vsw_addr_size
        );
        if (bytes_recvd == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        else if (bytes_recvd <= ETHER_HDR_LEN + VSW_CTRL_HDR_SIZE) {
            continue;
        }
        // Write the frame to the TAP
        ether_bytes_written = write(
            vwire->tapfd,
            buf + VSW_CTRL_HDR_SIZE,
            bytes_recvd - VSW_CTRL_HDR_SIZE
        );
        if (ether_bytes_written == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: vwire [ADDR] [TAP]\n");
        exit(EXIT_FAILURE);
    }
    char log_buf[128];

    // Open TAP interface
    int tapfd = vw_tap_alloc(argv[2]);
    snprintf(log_buf, sizeof(log_buf), "Successfully opened TAP interface: '%s'", argv[2]);
    log_message(log_buf, LOG_INFO);

    // Set up underlay UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }

    // Group related resources into one passable object
    vwire_t vwire = {
        .tapfd = tapfd,
        .sockfd = sockfd,
        .vsw_addr = {
            .sin_family = AF_INET,
            .sin_addr = {
                .s_addr = inet_addr(argv[1])
            },
            .sin_port = htons(9200)
        }
    };

    int ret = vw_conn_vswitch(&vwire);
    switch (ret)
    {
        case VW_VSWITCH_UNREACHABLE:
            log_message("Connection to vswitch timed-out.", LOG_ERROR);
            exit(EXIT_FAILURE);
            break;
        case VSW_CTRL_MSG_FULL:
            log_message("Cannot connect to vswitch since it is full.", LOG_ERROR);
            exit(EXIT_FAILURE);
            break;
        default:
            log_message("Successfully connected to the vswitch.", LOG_INFO);
            break;
    }

    pthread_t send_thread_pid;
    pthread_t recv_thread_pid;
    pthread_t heartbeat_thread_pid;

    // Start two threads - one for receiving and one for sending
    pthread_create(&send_thread_pid, NULL, &vw_send_data_to_vswitch, &vwire);
    pthread_create(&recv_thread_pid, NULL, &vw_recv_data_from_vswitch, &vwire);
    pthread_create(&heartbeat_thread_pid, NULL, &vw_send_heartbeats, &vwire);

    pthread_join(send_thread_pid, NULL);
    pthread_join(recv_thread_pid, NULL);
    pthread_join(heartbeat_thread_pid, NULL);

    // Close TAP interface UDP socket
    close(tapfd);
    close(sockfd);

    return EXIT_SUCCESS;
}
