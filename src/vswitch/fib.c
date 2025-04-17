#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/ethernet.h>
#include "util/logging.h"
#include "vswitch/fib.h"


size_t vsw_mac_table_to_str(entry_t *tbl, vport_t *vports, char **buf) {
    const size_t OUT_BASE_SIZE = 1024;
    const size_t MAX_ROW_SIZE = 64;
    char heading[] = "MAC ADDR               VPORT                AGE\n==========================================================\n";

    size_t buf_size = OUT_BASE_SIZE;
    size_t str_size = 0;
    ssize_t row_size;
    int vport_idx;
    char *mac_addr_buf;
    char ip_addr_buf[INET_ADDRSTRLEN];
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

    for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
        if (!tbl[i].used) {
            continue;
        }
        // Convert entry MAC address to string
        mac_addr_buf = ether_ntoa((const struct ether_addr *)tbl[i].mac_addr);

        // Convert socket IP address to string
        if (inet_ntop(AF_INET, &tbl[i].vport->addr.sin_addr, ip_addr_buf, sizeof(ip_addr_buf)) == NULL) {
            log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
            exit(EXIT_FAILURE);
        }

        // Find the source vport number
        vport_idx = -1;
        for (int j = 0; j < VSW_VPORT_ARR_SIZE; j++) {
            if (tbl[i].vport == &(vports[j])) {
                vport_idx = j;
            }
        }
        assert(vport_idx != -1);

        // Construct row
        row_size = snprintf(row_buf, MAX_ROW_SIZE, "%-17s      Virt/%-3d             %ld\n", mac_addr_buf, vport_idx + 1, time(NULL) - tbl[i].created);

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
