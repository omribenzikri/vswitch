#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include "util/logging.h"
#include "vswitch/stats.h"
#include "vswitch/vport.h"


size_t vsw_vport_table_to_str(vsw_vport_t *vports, size_t count, char **buf) {
    const size_t OUT_BASE_SIZE = 1024 * sizeof(char);
    const size_t MAX_ROW_LEN = 64;
    char *heading[] = {"NAME", "UNDERLAY SOCKET", "LAST SEEN"};

    size_t buf_size = OUT_BASE_SIZE;
    size_t out_len = 0;
    ssize_t row_len;
    char addr_buf[INET_ADDRSTRLEN];
    char sock_pair_buf[INET_ADDRSTRLEN + 5 + 1]; // +5 for port and +1 for delimeter
    char row_buf[MAX_ROW_LEN];
    
    // Heap-allocate base buffer
    char *tmp_buf = malloc(buf_size);
    if (tmp_buf == NULL) {
        log_sys_error();
        exit(EXIT_FAILURE); 
    }

    // Copy heading to the buffer
    out_len += snprintf(
        tmp_buf,
        buf_size,
        "%-16s%-27s%s\n===========================================================\n",
        heading[0], heading[1], heading[2]
    );

    for (int i = 0; i < count; i++) {
        if (!vports[i].used) {
            continue;
        }

        // Convert socket IP address to string
        if (inet_ntop(AF_INET, &vports[i].addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
            log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
            exit(EXIT_FAILURE);
        }

        // Join vport IP address + source port
        snprintf(sock_pair_buf, sizeof(sock_pair_buf), "%s:%u", addr_buf, vports[i].addr.sin_port);

        // Construct row
        row_len = snprintf(row_buf, MAX_ROW_LEN, "Virt/%-11u%-27s%ld seconds ago\n", i + 1, sock_pair_buf, time(NULL) - vports[i].last_seen);

        // Allocate more memory if needed
        out_len += row_len;
        if (out_len * sizeof(char) >= buf_size) {
            buf_size += OUT_BASE_SIZE;
            tmp_buf = realloc(tmp_buf, buf_size);
            if (tmp_buf == NULL) {
                log_sys_error();
                exit(EXIT_FAILURE); 
            }
        }

        // Concatenate new row
        strncat(tmp_buf, row_buf, row_len);
    }
    *buf = tmp_buf;
    return out_len;
}


size_t vsw_vport_stats_table_to_str(vsw_vport_t *vports, size_t count, char **buf){
    const size_t OUT_BASE_SIZE = 1024 * sizeof(char);
    const size_t MAX_ROW_LEN = 256;
    char *heading[] = {"NAME", "RX BYTES", "RX PACKETS", "TX BYTES", "TX PACKETS", "DROPS"};

    size_t buf_size = OUT_BASE_SIZE;
    size_t out_len = 0;
    ssize_t row_len;
    char row_buf[MAX_ROW_LEN];
    
    // Heap-allocate base buffer
    char *tmp_buf = malloc(buf_size);
    if (tmp_buf == NULL) {
        log_sys_error();
        exit(EXIT_FAILURE); 
    }

    // Copy heading to the buffer
    out_len += snprintf(
        tmp_buf,
        buf_size,
        "%-16s%-23s%-23s%-23s%-23s%-23s\n====================================================================================================================\n",
        heading[0], heading[1], heading[2], heading[3], heading[4], heading[5]
    );

    for (int i = 0; i < count; i++) {
        if (!vports[i].used) {
            continue;
        }

        // Construct row
        row_len = snprintf(
            row_buf,
            MAX_ROW_LEN,
            "Virt/%-11u%-23lu%-23lu%-23lu%-23lu%-23lu\n",
            i + 1,
            vports[i].stats.rx_bytes,
            vports[i].stats.rx_packets,
            vports[i].stats.tx_bytes,
            vports[i].stats.tx_packets,
            vports[i].stats.drops
        );

        // Allocate more memory if needed
        out_len += row_len;
        if (out_len * sizeof(char) >= buf_size) {
            buf_size += OUT_BASE_SIZE;
            tmp_buf = realloc(tmp_buf, buf_size);
            if (tmp_buf == NULL) {
                log_sys_error();
                exit(EXIT_FAILURE); 
            }
        }

        // Concatenate new row
        strncat(tmp_buf, row_buf, row_len);
    }
    *buf = tmp_buf;
    return out_len;
}


vsw_vport_t *vsw_add_vport(vsw_vport_t *vports, size_t count, struct sockaddr_in *addr) {
    vsw_vport_t new = {
        .used = true,
        .last_seen = time(NULL),
        .addr = *addr
    };
    memset(&new.stats, 0, sizeof(new.stats));

    for (int i = 0; i < count; i++) {
        if (vports[i].used) {
            // Interface already exists, just return
            if (addr->sin_addr.s_addr == vports[i].addr.sin_addr.s_addr && addr->sin_port == vports[i].addr.sin_port) {
                return &vports[i];
            }
            continue;
        }
        // Empty slot found, insert here
        else {
            vports[i] = new;
            return &vports[i];
        }
    }
    return NULL;
}


vsw_vport_t *vsw_search_vport(vsw_vport_t *vports, size_t count, struct sockaddr_in *vport_addr) {
    for (int i = 0; i < count; i++) {
        if (!vports[i].used) {
            continue;
        }
        if (vport_addr->sin_addr.s_addr == vports[i].addr.sin_addr.s_addr && vport_addr->sin_port == vports[i].addr.sin_port) {
            return &vports[i];
        }
    }
    return NULL;
}


int vsw_search_vport_idx(vsw_vport_t *vports, size_t count, struct sockaddr_in *vport_addr) {
    for (int i = 0; i < count; i++) {
        if (!vports[i].used) {
            continue;
        }
        if (vport_addr->sin_addr.s_addr == vports[i].addr.sin_addr.s_addr && vport_addr->sin_port == vports[i].addr.sin_port) {
            return i;
        }
    }
    return -1;
}