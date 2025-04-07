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
#include <assert.h>
#include <stdbool.h>
#include <pthread.h>
#include "proto.h"

#define VP_HRTBT_INTERVAL 5
#define VP_JOIN_MAX_RETRIES 5
#define VP_RECV_TIMEOUT 2
#define VP_VSWITCH_UNREACHABLE -1

// Represents an interface in the virtual overlay network
typedef struct {
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;
} vport_t;

/*
Obtain ownership and return a file descriptor for TAP interface
*/
int vp_tap_alloc(char *name) {
    const int tapfd = open("/dev/net/tun", O_RDWR);
    if (tapfd == -1) {
        perror("vport");
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
        perror("vport");
        exit(EXIT_FAILURE);
    }
    return tapfd;
}

/*
Attemps to connect to the vswitch by continously sending join requests
until a response is received from the vswitch. If a response was received, it is returned,
otherwise, -1 returned.
*/
int vp_conn_vswitch(const vport_t *vport) {
    uint8_t req = VSW_TUN_MSG_JOIN;
    uint8_t res;
    ssize_t bytes_recvd;
    ssize_t bytes_sent;
    socklen_t vsw_addr_size;
    int ret;

    struct timeval timeout_tv = {
        .tv_sec = VP_RECV_TIMEOUT,
        .tv_usec = 0
    };
    struct timeval default_tv = {
        .tv_sec = 0,
        .tv_usec = 0
    };
    
    // Set a timeout for the recv call
    ret = setsockopt(vport->sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout_tv, sizeof(timeout_tv));
    if (ret == -1) {
        perror("vport");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < VP_JOIN_MAX_RETRIES; i++) {
        // Send a join request to the vswitch
        bytes_sent = sendto(
            vport->sockfd,
            &req,
            sizeof(req),
            0,
            (struct sockaddr *)&vport->vsw_addr,
            sizeof(vport->vsw_addr)
        );
        if (bytes_sent == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
        // Wait for a response from the vswitch
        bytes_recvd = recvfrom(
            vport->sockfd,
            &res,
            sizeof(res),
            0,
            (struct sockaddr *)&vport->vsw_addr,
            &vsw_addr_size
        );
        if (bytes_recvd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            perror("vport");
            exit(EXIT_FAILURE);
        }
        // Response received from vswitch
        else if (bytes_recvd > 0) {
            // Set timeout to the default
            ret = setsockopt(vport->sockfd, SOL_SOCKET, SO_RCVTIMEO, &default_tv, sizeof(default_tv));
            if (ret == -1) {
                perror("vport");
                exit(EXIT_FAILURE);
            }
            return res;
        }
    }

    // No response received from vswitch, set timeout to the default
    ret = setsockopt(vport->sockfd, SOL_SOCKET, SO_RCVTIMEO, &default_tv, sizeof(default_tv));
    if (ret == -1) {
        perror("vport");
        exit(EXIT_FAILURE);
    }
    return -1;
}

/*
Continously sends heartbeats messages to the vswitch to let it know the vport
is still alive.
*/
void *vp_send_heartbeats(void *_vport) {
    const vport_t *vport = (const vport_t *)_vport;
    uint8_t msg = VSW_TUN_MSG_HRTBT;
    ssize_t bytes_sent;

    // Send heartbeats in an infinite loop
    while (true) {
        bytes_sent = sendto(
            vport->sockfd,
            &msg,
            sizeof(msg),
            0,
            (struct sockaddr *)&vport->vsw_addr,
            sizeof(vport->vsw_addr)
        );
        if (bytes_sent == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
        // Sleep until the next heartbeat
        sleep(VP_HRTBT_INTERVAL);
    }
}

/*
Continously reads incoming data from the TAP interface and when it arrives,
orwards it through the underlay UDP socket
*/
void *vp_forward_data_to_vswitch(void *_vport) {
    const vport_t *vport = (const vport_t *)_vport;

    // Buffer for storing read ethernet frames + extra bytes for custom tunnel encap
    uint8_t buf[ETHER_MAX_LEN + VSW_TUN_ENCAP_SIZE];
    ssize_t ether_bytes_read;

    // Set message type to regular traffic
    buf[0] = VSW_TUN_MSG_TRFC;
    ssize_t bytes_sent;

    while (true) {
        // Read an ethernet frame from the TAP interface
        ether_bytes_read = read(
            vport->tapfd,
            buf + VSW_TUN_ENCAP_SIZE,
            sizeof(buf) - VSW_TUN_ENCAP_SIZE
        );
        if (ether_bytes_read == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
        else if (ether_bytes_read == 0) {
            continue;
        }
        assert(ether_bytes_read > ETHER_ADDR_LEN);
    
        // Forward the frame through the socket
        bytes_sent = sendto(
            vport->sockfd,
            buf,
            ether_bytes_read + VSW_TUN_ENCAP_SIZE,
            0,
            (struct sockaddr *)&vport->vsw_addr,
            sizeof(vport->vsw_addr)
        );
        if (bytes_sent == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
    }
}

/*
Continously reads incoming data from the underlay socket and forwards it
through the TAP to the linux kernel networking stack
*/
void *vp_forward_data_to_tap(void *_vport) {
    const vport_t *vport = (const vport_t *)_vport;

    // Buffer for storing received ethernet frames
    uint8_t buf[ETHER_MAX_LEN + VSW_TUN_ENCAP_SIZE];
    ssize_t ether_bytes_written;
    ssize_t bytes_recvd;
    socklen_t vsw_addr_size;

    while (true) {
        // Receive a frame from the socket
        
        bytes_recvd = recvfrom(
            vport->sockfd,
            buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&vport->vsw_addr,
            &vsw_addr_size
        );
        if (bytes_recvd == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
        assert(bytes_recvd > ETHER_HDR_LEN + VSW_TUN_ENCAP_SIZE);
        // Write the frame to the TAP
        ether_bytes_written = write(
            vport->tapfd,
            buf + VSW_TUN_ENCAP_SIZE,
            bytes_recvd - VSW_TUN_ENCAP_SIZE
        );
        if (ether_bytes_written == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
    }
}

int main() {
    // Open TAP interface
    int tapfd = vp_tap_alloc("tap0");

    // Set up underlay UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("vport");
        exit(EXIT_FAILURE);
    }

    // Group related resources into one passable object
    vport_t vport = {
        .tapfd = tapfd,
        .sockfd = sockfd,
        .vsw_addr = {
            .sin_family = AF_INET,
            .sin_addr = inet_addr("192.168.64.5"),
            .sin_port = htons(9200)
        }
    };

    int ret = vp_conn_vswitch(&vport);
    switch (ret)
    {
        case VP_VSWITCH_UNREACHABLE:
            fprintf(stderr, "vport: connection to vswitch timed-out.\n");
            exit(EXIT_FAILURE);
            break;
        case VSW_TUN_MSG_FULL:
            fprintf(stderr, "vport: cannot connect to vswitch since it is full.\n");
            exit(EXIT_FAILURE);
            break;
        default:
            break;
    }

    pthread_t read_thread_pid;
    pthread_t write_thread_pid;
    pthread_t heartbeat_thread_pid;

    // Start two threads - one for receiving and one for sending
    pthread_create(&read_thread_pid, NULL, &vp_forward_data_to_vswitch, &vport);
    pthread_create(&write_thread_pid, NULL, &vp_forward_data_to_tap, &vport);
    pthread_create(&heartbeat_thread_pid, NULL, &vp_send_heartbeats, &vport);

    pthread_join(read_thread_pid, NULL);
    pthread_join(write_thread_pid, NULL);
    pthread_join(heartbeat_thread_pid, NULL);

    // Close TAP interface UDP socket
    close(tapfd);
    close(sockfd);

    return EXIT_SUCCESS;
}