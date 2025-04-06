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
#include <netinet/ether.h>
#include <net/ethernet.h>
#include <assert.h>
#include <pthread.h>
#include "common.h"

#define VSW_MAC_ADDR_TBL_SIZE 32
#define VSW_VPORT_ARR_SIZE 24

// Represents a virtual interface
typedef struct {
    bool used;
    time_t last_seen;
    struct sockaddr_in addr;
} vport_t;

// Represents MAC address table entry
typedef struct {
    bool used;
    time_t created;
    uint8_t mac_addr[ETHER_ADDR_LEN];
    vport_t *vport;
} entry_t;

// Different opertional modes of a switch
typedef enum {
    OPER_FLOOD,
    OPER_FORWARD,
    OPER_FILTER
} oper_mode_t;


/*
Prints the contents of the MAC address table to stdout.
*/
void vsw_print_mac_table(entry_t *tbl) {
    printf("MAC ADDR               VPORT                       AGE\n");
    printf("======================================================\n");
    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            continue;
        }
        // Convert entry MAC address to string
        char *mac_addr_str = ether_ntoa((const struct ether_addr *)tbl[i].mac_addr);

        // Convert socket IP address to string
        char ip_addr_buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &tbl[i].vport->addr.sin_addr, ip_addr_buf, sizeof(ip_addr_buf)) == NULL) {
            fprintf(stderr, "Failed to stringify vport IP address.\n");
            exit(EXIT_FAILURE);
        }
        printf("%s      Virt/%-3d                    %ld\n", mac_addr_str, i + 9, time(NULL) - tbl[i].created);
    }
}

/*
Prints the contents of the virtual interface array to stdout.
*/
void vsw_print_vport_table(vport_t *arr) {
    printf("NAME            UNDERLAY SOCKET            LAST SEEN\n");
    printf("==========================================================\n");
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        if (!arr[i].used) {
            continue;
        }

        // Convert socket IP address to string
        char addr_buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &arr[i].addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
            fprintf(stderr, "Failed to stringify vport IP address.\n");
            exit(EXIT_FAILURE);
        }

        // Join the IP address + port strings, 5 being the max numbers of digits in the max value of a port (stored in 16 bits)
        // and +1 for the delimiting colon
        char sock_pair_buf[INET_ADDRSTRLEN + 5 + 1];
        snprintf(sock_pair_buf, sizeof(sock_pair_buf), "%s:%u", addr_buf, arr[i].addr.sin_port);

        printf("Virt/%-3u        %-22s     %ld seconds ago\n", i + 1, sock_pair_buf, time(NULL) - arr[i].last_seen);
    }
}

/*
Adds a new entry to the MAC address table. If full, the new entry
will override the oldest entry in the table (by age parameter).
*/
void vsw_add_mac_entry(entry_t *tbl, uint8_t *mac_addr, vport_t *vport) {
    int old_idx = 0;
    time_t old_age = LONG_MAX;
    entry_t new = {
        .used = true,
        .vport = vport,
        .created = time(NULL)
    };
    memcpy(new.mac_addr, mac_addr, ETHER_ADDR_LEN);

    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        // If an entry for this MAC addres already exists, just return
        if (memcmp(tbl[i].mac_addr, mac_addr, ETHER_ADDR_LEN) == 0) {
            return;
        }
        // Unused slot found, insert the entry here
        if (!tbl[i].used) {
            tbl[i] = new;
            return;
        // Look for the oldest entry in the list
        } else if (tbl[i].created < old_age) {
            old_age = tbl[i].created;
            old_idx = i;
        }
    }

    // Replace the oldest entry in the table
    tbl[old_idx] = new;
}

/*
Adds a new interface to the array of interfaces.
If there is room for a new connection, returns a pointer to the added object,
otherwise, returns NULL
*/
vport_t *vsw_add_vport(vport_t *arr, struct sockaddr_in *addr) {
    vport_t new = {
        .used = true,
        .last_seen = time(NULL),
        .addr = *addr
    };
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        // Interface already exists, just return
        if (addr->sin_addr.s_addr == arr[i].addr.sin_addr.s_addr && addr->sin_port == arr[i].addr.sin_port) {
            return &arr[i];
        }
        // Empty slot found, insert here
        if (!arr[i].used) {
            arr[i] = new;
            return &arr[i];
        }
    }
    return NULL;
}

/*
Searches for a relavent entry in the MAC address table for
the given destination address. If an entry is found, returns a pointer to the struct,
otherwise, returns NULL
*/
entry_t *vsw_search_mac_entry(entry_t *tbl, uint8_t *mac_addr) {
    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            continue;
        }
        if (memcmp(tbl[i].mac_addr, mac_addr, ETHER_ADDR_LEN) == 0) {
            return &tbl[i];
        }
    }
    return NULL;
}

/*
Searches for a virtual interface in the interfaces array by socket pair.
If found - returns a pointer to the struct, otherwise, returns NULL
*/
vport_t *vsw_search_vport(vport_t *arr, struct sockaddr_in *vport_addr) {
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        if (vport_addr->sin_addr.s_addr == arr[i].addr.sin_addr.s_addr && vport_addr->sin_port == arr[i].addr.sin_port) {
            return &arr[i];
        }
    }
    return NULL;
}

/*
Looks up the given destination MAC in the table in order to find a matching entry.
If a matching entry is found - it is saved in the provided pointer, otherwise, NULL is saved.
Based on whether an entry was found and based on the entry itself, the matching operation mode
is returned.
*/
oper_mode_t vsw_determine_oper_mode(entry_t *mac_addr_tbl, uint8_t *mac_addr, vport_t *src_vport, entry_t **entry_ptr) {
    // L2 broadcast address
    const uint8_t L2_BRDCST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Check if frame is broadcast
    if (memcmp(mac_addr, L2_BRDCST_ADDR, ETHER_ADDR_LEN) == 0) {
        *entry_ptr = NULL;
        return OPER_FLOOD;
    } else {
        // Search the MAC address table for the dest address
        *entry_ptr = vsw_search_mac_entry(mac_addr_tbl, mac_addr);
        // No entry flood - need to flood unknown unicast
        if (*entry_ptr == NULL) {
            return OPER_FLOOD;
        }
        // Destination and source interfaces are the same, need to filter
        if ((*entry_ptr)->vport == src_vport) {
            return OPER_FILTER;
        }
        // Valid entry found - forward traffic
        return OPER_FORWARD;
    }
}

/*
Floods received ethernet frame through all connected interfaces, except
for the source interface.
*/
void vsw_flood_frame(int sockfd, vport_t *vport_arr, vport_t *src_vport, uint8_t *frame, ssize_t frame_size) {
    // Flood the frame through all interfaces except the source
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        // Skip flooding the traffic trough the source interface
        if (!vport_arr[i].used || &vport_arr[i] == src_vport) {
            continue;
        }
        sendto(
            sockfd,
            frame,
            frame_size,
            0,
            (struct sockaddr *)&vport_arr[i].addr,
            sizeof(struct sockaddr_in)
        );
    }
}

/*
Forwards received ethernet frame through the provided virtual interface.
*/
void vsw_forward_frame(int sockfd, vport_t *vport, uint8_t *frame, ssize_t frame_size) {
    sendto(
        sockfd,
        frame,
        frame_size,
        0,
        (struct sockaddr *)&(vport->addr),
        sizeof(struct sockaddr_in)
    );
}


void *vsw_print_ui(void *_tbl) {
    vport_t *tbl = (vport_t *)_tbl;
    while (true) {
        system("clear");
        vsw_print_vport_table(tbl);
        sleep(1);
    }
}


int main() {
    // Set up the MAC address table
    entry_t mac_addr_tbl[VSW_MAC_ADDR_TBL_SIZE];
    memset(mac_addr_tbl, 0, sizeof(mac_addr_tbl));

    // Set up the interface table
    vport_t vport_arr[VSW_VPORT_ARR_SIZE];
    memset(vport_arr, 0, sizeof(vport_arr));

    // Set up vswitch UDP socket
    struct sockaddr_in vsw_addr = {
        .sin_family = AF_INET,
        .sin_addr = htonl(INADDR_ANY),
        .sin_port = htons(9200)
    };
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("vswitch");
        return EXIT_FAILURE;
    }

    // Bind vswitch UDP socket
    int ret = bind(sockfd, (struct sockaddr *)&vsw_addr, sizeof(vsw_addr));
    if (ret == -1) {
        perror("vswitch");
        return EXIT_FAILURE;
    }

    // Start UI thread
    pthread_t ui_thread_pid;
    pthread_create(&ui_thread_pid, NULL, &vsw_print_ui, &vport_arr);

    struct sockaddr_in vp_addr;
    socklen_t vp_addr_len = sizeof(vp_addr);
    uint8_t buf[ETHER_MAX_LEN + VSW_TUN_ENCAP_SIZE];

    while (true) {
        // Receive a frame from the socket
        ssize_t bytes_recvd = recvfrom(
            sockfd,
            buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&vp_addr,
            &vp_addr_len
        );
        if (bytes_recvd == -1) {
            perror("vswitch");
            return EXIT_FAILURE;
        }
        assert(bytes_recvd > 0);

        uint8_t frame_type = *buf;
        switch (frame_type)
        {
            case VSW_TUN_MSG_HRTBT:
                // Heartbeat received - search for the vport by address
                vport_t *vport = vsw_search_vport(vport_arr, &vp_addr);
                if (vport == NULL) {
                    continue;
                }
                // Reset the last seen parameter
                vport->last_seen = time(NULL); 
                break;
            case VSW_TUN_MSG_JOIN:
                // Join message received - add a new virtual interface connection
                vport_t *new_vport = vsw_add_vport(vport_arr, &vp_addr);
                // Format an appropriate response
                uint8_t response = new_vport == NULL ? VSW_TUN_MSG_FULL : VSW_TUN_MSG_FREE;
                sendto(
                    sockfd,
                    &response,
                    sizeof(response),
                    0,
                    (struct sockaddr *)&vp_addr,
                    sizeof(struct sockaddr_in)
                );
                break;
            case VSW_TUN_MSG_TRFC:
                // Strip the custom tunnel header
                uint8_t *ether_frame = buf + VSW_TUN_ENCAP_SIZE;
                size_t ether_frame_size = bytes_recvd - VSW_TUN_ENCAP_SIZE;
                
                // Parse ethernet header
                assert(ether_frame_size > ETHER_HDR_LEN);
                struct ether_header *hdr = (struct ether_header *)ether_frame;

                // Search for the interface by address
                vport_t *src_vport = vsw_search_vport(vport_arr, &vp_addr);
                if (src_vport == NULL) {
                    continue;
                }

                // Add new MAC address table entry
                vsw_add_mac_entry(mac_addr_tbl, hdr->ether_shost, src_vport);

                // Determine what to do with the frame
                entry_t *dst_entry;
                oper_mode_t mode = vsw_determine_oper_mode(mac_addr_tbl, hdr->ether_dhost, src_vport, &dst_entry);

                switch (mode)
                {
                    case OPER_FLOOD:
                        vsw_flood_frame(sockfd, vport_arr, src_vport, buf, bytes_recvd);
                        break;
                    case OPER_FORWARD:
                        vsw_forward_frame(sockfd, dst_entry->vport, buf, bytes_recvd);
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }

    // Close UDP socket
    close(sockfd);

    return EXIT_SUCCESS;
}
