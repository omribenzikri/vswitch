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

// Represents an interface in the virtual overlay network
typedef struct {
    int tapfd;
    int sockfd;
    struct sockaddr_in vsw_addr;
} vport_t;


int tap_alloc() {
    const int tapfd = open("/dev/net/tun", O_RDWR);
    if (tapfd == -1) {
        perror("vport");
        exit(EXIT_FAILURE);
    }
    // Set up a request for our TAP
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", "tap0");

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
void forward_data_to_vswitch(vport_t *vport) {
    // Buffer for storing read ethernet frames
    uint8_t ether_frame[ETHER_MAX_LEN];
    ssize_t ether_bytes_read;

    while (true) {
        // Read an ethernet frame from the TAP interface
        ether_bytes_read = read(vport->tapfd, ether_frame, sizeof(ether_frame));
        if (ether_bytes_read == -1) {
            perror("vport");
            exit(EXIT_FAILURE);
        }
        else if (ether_bytes_read == 0) {
            continue;
        }
        assert(ether_bytes_read > 14);
    
        // Forward the frame through the socket
        sendto(
            vport->sockfd,
            ether_frame,
            ether_bytes_read,
            0,
            (struct sockaddr *)&vport->vsw_addr,
            sizeof(vport->vsw_addr)
        );
    }
}


int main() {
    const int tapfd = tap_alloc();

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    vport_t vport = {
        .tapfd = tapfd,
        .sockfd = sockfd,
        .vsw_addr = {
            .sin_family = AF_INET,
            .sin_addr = inet_addr("127.0.0.1"),
            .sin_port = htons(9200)
        }
    };

    forward_data_to_vswitch(&vport);

    close(tapfd);
    close(sockfd);

    return EXIT_SUCCESS;
}