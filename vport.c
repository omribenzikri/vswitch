#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
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
#include "common.h"

// Represents an interface in the virtual overlay network
typedef struct {
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;
} vport_t;

/*
Get a file descriptor for TAP interface
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
Continously reads incoming data from the TAP interface and when it arrives,
orwards it through the underlay UDP socket
*/
void *vp_forward_data_to_vswitch(void *_vport) {
    vport_t *vport = (vport_t *)_vport;

    // Buffer for storing read ethernet frames + extra bytes for custom tunnel encap
    uint8_t buf[ETHER_MAX_LEN + VSW_TUN_ENCAP_SIZE];
    ssize_t ether_bytes_read;

    // Set message type to regular traffic
    buf[0] = VSW_TUN_MSG_TRFC;

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
        sendto(
            vport->sockfd,
            buf,
            ether_bytes_read + VSW_TUN_ENCAP_SIZE,
            0,
            (struct sockaddr *)&vport->vsw_addr,
            sizeof(vport->vsw_addr)
        );
    }
}

/*
Continously reads incoming data from the underlay socket and forwards it
through the TAP to the linux kernel networking stack
*/
void *vp_forward_data_to_tap(void *_vport) {
    vport_t *vport = (vport_t *)_vport;

    // Buffer for storing received ethernet frames
    uint8_t buf[ETHER_MAX_LEN + VSW_TUN_ENCAP_SIZE];
    ssize_t bytes_recvd;
    socklen_t vsw_addr_size;
    int ret;

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
        ret = write(
            vport->tapfd,
            buf + VSW_TUN_ENCAP_SIZE,
            bytes_recvd - VSW_TUN_ENCAP_SIZE
        );
        if (ret == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
    }
}

int main() {
    // Open TAP interface
    const int tapfd = vp_tap_alloc("tap0");

    // Set up underlay UDP socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    vport_t vport = {
        .tapfd = tapfd,
        .sockfd = sockfd,
        .vsw_addr = {
            .sin_family = AF_INET,
            .sin_addr = inet_addr("192.168.64.5"),
            .sin_port = htons(9200)
        }
    };

    pthread_t read_thread_id;
    pthread_t write_thread_id;

    // Start two threads - one for receiving and one for sending
    pthread_create(&read_thread_id, NULL, &vp_forward_data_to_vswitch, &vport);
    pthread_create(&write_thread_id, NULL, &vp_forward_data_to_tap, &vport);

    pthread_join(read_thread_id, NULL);
    pthread_join(write_thread_id, NULL);

    // Close TAP interface UDP socket
    close(tapfd);
    close(sockfd);

    return EXIT_SUCCESS;
}