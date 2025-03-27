#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <assert.h>

#define VSW_MAC_ADDR_TBL_SIZE 32

// Represents MAC address table entry
typedef struct {
    bool used;
    time_t created;
    uint8_t mac_addr[ETHER_ADDR_LEN];
    struct sockaddr_in *vport;
} entry_t;


// Prints the contents of the MAC address table to stdout.
void vsw_print_mac_table(entry_t *tbl) {
    printf("MAC ADDR           VPORT                     AGE\n");
    printf("================================================\n");
    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            continue;
        }
        for (int j = 0; j < ETHER_ADDR_LEN; j++) {
            printf("%02x", tbl[i].mac_addr[j]);
            if (j == 1 || j == 3) {
                printf("%c", '.');
            }
        }

        char addr_buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &tbl[i].vport->sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
            fprintf(stderr, "Failed to stringify vport IP address.\n");
            exit(EXIT_FAILURE);
        }
        printf("     %s:%u          %ld\n", addr_buf, tbl[i].vport->sin_port, time(NULL) - tbl[i].created);
    }
}

/*
Adds a new entry to the MAC address table. If full, the new entry
will override the oldest entry in the table (by age parameter).
The entry index is returned.
*/
int vsw_add_entry(entry_t *tbl, uint8_t *mac_addr, struct sockaddr_in *vport) {
    int old_idx = 0;
    time_t old_age = LONG_MAX;
    entry_t new = {
        .used = true,
        .vport = vport,
        .created = time(NULL)
    };
    memcpy(new.mac_addr, mac_addr, ETHER_ADDR_LEN);

    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        // If an entry for this MAC addres already exists, just reset the age
        if (memcmp(tbl[i].mac_addr, mac_addr, ETHER_ADDR_LEN) == 0) {
            tbl[i].created = time(NULL);
            return i;
        }
        // Unused slot found, insert the entry here
        if (!tbl[i].used) {
            tbl[i] = new;
            return i;
        // Look for the oldest entry in the list
        } else if (tbl[i].created < old_age) {
            old_age = tbl[i].created;
            old_idx = i;
        }
    }

    // Replace the oldest entry in the table
    tbl[old_idx] = new;
    return old_idx;
}

/*
Searches for a relavent entry in the MAC address table for
the given destination address. If an entry is found, it's index is returned,
otherwise, -1 is returned.
*/
int vsw_search_entry(entry_t *tbl, uint8_t *mac_addr) {
    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            continue;
        }
        else if (memcmp(tbl[i].mac_addr, mac_addr, ETHER_ADDR_LEN) == 0) {
            return i;
        }
    }
    return -1;
}


int main() {
    // Set up the MAC address table
    entry_t mac_addr_tbl[VSW_MAC_ADDR_TBL_SIZE] = {0};

    // Set up vswitch UDP socket
    struct sockaddr_in vsw_addr = {
        .sin_family = AF_INET,
        .sin_addr = htonl(INADDR_ANY),
        .sin_port = htons(9200)
    };
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // Bind vswitch UDP socket
    int ret = bind(sockfd, (struct sockaddr *)&vsw_addr, sizeof(vsw_addr));
    if (ret == -1) {
        perror("vswitch");
        return EXIT_FAILURE;
    }

    // Buffers for storing received ethernet frames and their headers
    uint8_t frame_buf[ETHER_MAX_LEN];
    uint8_t hdr_buf[ETHER_HDR_LEN];

    struct sockaddr_in vport_addr;
    socklen_t vport_addr_len = sizeof(vport_addr);

    while (true) {
        // Receive a frame from the socket
        int ether_bytes_recvd = recvfrom(
            sockfd,
            frame_buf,
            sizeof(frame_buf),
            0,
            (struct sockaddr *)&vport_addr,
            &vport_addr_len
        );
        if (ether_bytes_recvd == -1) {
            perror("vswitch");
            return EXIT_FAILURE;
        }
        assert(ether_bytes_recvd > ETHER_HDR_LEN);
    
        // Parse the header (first 14 bytes of the frame)
        struct ether_header *hdr = (struct ether_header *)frame_buf;

        // Add the source address to the table
        int src_entry_idx = vsw_add_entry(mac_addr_tbl, hdr->ether_shost, &vport_addr);

        system("clear");
        vsw_print_mac_table(mac_addr_tbl);

        int dst_entry_idx = vsw_search_entry(mac_addr_tbl, hdr->ether_dhost);
        if (dst_entry_idx = -1) {
            // Flood the frame through all interfaces except the source
            for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
                if (!mac_addr_tbl[i].used || i == src_entry_idx) {
                    continue;
                }
                sendto(
                    sockfd,
                    frame_buf,
                    ether_bytes_recvd,
                    0,
                    (struct sockaddr *)&mac_addr_tbl[i].vport,
                    vport_addr_len
                );
            }
        } else {
            // Forward the frame through the socket
            sendto(
                sockfd,
                frame_buf,
                ether_bytes_recvd,
                0,
                (struct sockaddr *)&mac_addr_tbl[dst_entry_idx].vport,
                vport_addr_len
            );
        }
    }

    close(sockfd);

    return EXIT_SUCCESS;
}
