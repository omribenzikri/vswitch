#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include "util/logging.h"
#include "vswitch/stats.h"
#include "vswitch/vport.h"

#define VSW_VPORT_ARR_SIZE 24


size_t vsw_vport_table_to_str(vport_t *vports, char **buf) {
    const size_t OUT_BASE_SIZE = 1024;
    const size_t MAX_ROW_SIZE = 64;
    char heading[] = "NAME            UNDERLAY SOCKET            LAST SEEN\n==========================================================\n";

    size_t buf_size = OUT_BASE_SIZE;
    size_t str_size = 0;
    ssize_t row_size;
    char addr_buf[INET_ADDRSTRLEN];
    char sock_pair_buf[INET_ADDRSTRLEN + 5 + 1]; // +5 for port and +1 for delimeter
    char row_buf[MAX_ROW_SIZE];
    
    // Heap-allocate base buffer
    char *tmp_buf = malloc(buf_size * sizeof(char));
    if (tmp_buf == NULL) {
        log_sys_error();
        exit(EXIT_FAILURE); 
    }

    // Copy heading to the buffer
    strcpy(tmp_buf, heading);
    str_size += sizeof(heading);

    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        if (!vports[i].used) {
            continue;
        }

        // Convert socket IP address to string
        if (inet_ntop(AF_INET, &vports[i].addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
            log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
            exit(EXIT_FAILURE);
        }

        // Joim vport IP address + source port
        snprintf(sock_pair_buf, sizeof(sock_pair_buf), "%s:%u", addr_buf, vports[i].addr.sin_port);

        // Construct row
        row_size = snprintf(row_buf, MAX_ROW_SIZE, "Virt/%-3u        %-22s     %ld seconds ago\n", i + 1, sock_pair_buf, time(NULL) - vports[i].last_seen);

        // Allocate more memory if needed
        str_size += row_size;
        if (str_size >= buf_size) {
            buf_size += OUT_BASE_SIZE;
            tmp_buf = realloc(tmp_buf, buf_size * sizeof(char));
            if (tmp_buf == NULL) {
                log_sys_error();
                exit(EXIT_FAILURE); 
            }
        }

        // Concatenate new row (+1 for null terminator)
        strncat(tmp_buf, row_buf, row_size + 1);
    }
    *buf = tmp_buf;
    return str_size - 1;
}


size_t vsw_vport_stats_table_to_str(vport_t *vports, char **buf) {
    const size_t OUT_BASE_SIZE = 1024;
    const size_t MAX_ROW_SIZE = 256;
    char heading[] = "NAME                      RX BYTES               RX PACKETS             TX BYTES               TX PACKETS             DROPS\n==============================================================================================================================\n";

    size_t buf_size = OUT_BASE_SIZE;
    size_t str_size = 0;
    ssize_t row_size;
    char row_buf[MAX_ROW_SIZE];
    
    // Heap-allocate base buffer
    char *tmp_buf = malloc(buf_size * sizeof(char));
    if (tmp_buf == NULL) {
        log_sys_error();
        exit(EXIT_FAILURE); 
    }

    // Copy heading to the buffer
    strcpy(tmp_buf, heading);
    str_size += sizeof(heading);

    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        if (!vports[i].used) {
            continue;
        }

        // Construct row
        row_size = snprintf(
            row_buf,
            MAX_ROW_SIZE,
            "Virt/%-3u                  %-20lu   %-20lu   %-20lu   %-20lu   %-20lu\n",
            i + 1,
            vports[i].stats.rx_bytes,
            vports[i].stats.rx_packets,
            vports[i].stats.tx_bytes,
            vports[i].stats.tx_packets,
            vports[i].stats.drops
        );

        // Allocate more memory if needed
        str_size += row_size;
        if (str_size >= buf_size) {
            buf_size += OUT_BASE_SIZE;
            tmp_buf = realloc(tmp_buf, buf_size * sizeof(char));
            if (tmp_buf == NULL) {
                log_sys_error();
                exit(EXIT_FAILURE); 
            }
        }

        // Concatenate new row (+1 for null terminator)
        strncat(tmp_buf, row_buf, row_size + 1);
    }
    *buf = tmp_buf;
    return str_size - 1;
}


vport_t *vsw_add_vport(vport_t *vports, struct sockaddr_in *addr) {
    vport_t new = {
        .used = true,
        .last_seen = time(NULL),
        .addr = *addr
    };
    memset(&new.stats, 0, sizeof(new.stats));

    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        // Interface already exists, just return
        if (addr->sin_addr.s_addr == vports[i].addr.sin_addr.s_addr && addr->sin_port == vports[i].addr.sin_port) {
            return &vports[i];
        }
        // Empty slot found, insert here
        if (!vports[i].used) {
            vports[i] = new;
            return &vports[i];
        }
    }
    return NULL;
}


vport_t *vsw_search_vport(vport_t *vports, struct sockaddr_in *vport_addr) {
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        if (vport_addr->sin_addr.s_addr == vports[i].addr.sin_addr.s_addr && vport_addr->sin_port == vports[i].addr.sin_port) {
            return &vports[i];
        }
    }
    return NULL;
}