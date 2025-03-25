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

#define VSW_MAC_ADDR_TBL_SIZE 32

// Represents a 48-bit MAC address, with 16 bits of padding of course
typedef union {
    uint8_t bytes[6];
    uint64_t raw;
} mac_addr_t;


// Represents MAC address table entry
typedef struct {
    bool used;
    mac_addr_t mac_addr;
    uint8_t iface;
    time_t created;
} entry_t;


// Prints the contents of the MAC address table to stdout.
void vsw_print_mac_table(entry_t *tbl) {
    printf("MAC ADDR           INT       AGE\n");
    printf("==================================\n");
    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            continue;
        }
        for (int j = 0; j < 6; j++) {
            printf("%02x", tbl[i].mac_addr.bytes[j]);
            if (j == 1 || j == 3) {
                printf("%c", '.');
            }
        }
        printf("     %d         %ld\n", tbl[i].iface, time(NULL) - tbl[i].created);
    }
}

/*
Adds a new entry to the MAC address table. If full, the new entry
will override the oldest entry in the table (by age parameter)
*/
void vsw_add_entry(entry_t *tbl, uint8_t *mac_addr, uint8_t iface) {
    int old_idx = 0;
    time_t old_age = LONG_MAX;
    entry_t new = {
        .used = true,
        .iface = iface,
        .created = time(NULL)
    };
    memcpy(new.mac_addr.bytes, mac_addr, 6);

    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            tbl[i] = new;
            return;
        } else if (tbl[i].created < old_age) {
            old_age = tbl[i].created;
            old_idx = i;
        }
    }

    // Replace the oldest entry in the table
    tbl[old_idx] = new;
}


int main() {
    entry_t mac_addr_tbl[VSW_MAC_ADDR_TBL_SIZE];
    memset(mac_addr_tbl, 0, VSW_MAC_ADDR_TBL_SIZE * sizeof(entry_t));

    struct sockaddr_in vsw_addr = {
        .sin_family = AF_INET,
        .sin_addr = htonl(INADDR_ANY),
        .sin_port = htons(9200)
    };
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    int ret = bind(sockfd, (struct sockaddr *)&vsw_addr, sizeof(vsw_addr));
    if (ret == -1) {
        perror("vswitch");
        return EXIT_FAILURE;
    }

    uint8_t e1_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t e2_mac[] = {0xAB, 0x55, 0x1A, 0xC7, 0x99, 0xFF};

    vsw_add_entry(mac_addr_tbl, e1_mac, 1);
    vsw_add_entry(mac_addr_tbl, e2_mac, 2);

    vsw_print_mac_table(mac_addr_tbl);

    close(sockfd);

    return EXIT_SUCCESS;
}
