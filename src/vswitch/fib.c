#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <net/ethernet.h>
#include "util/logging.h"
#include "vswitch/fib.h"

size_t vsw_mac_table_to_str(vsw_entry_t *tbl, size_t tbl_size, vsw_vport_t *vports, size_t vport_count, char **buf) {
    const size_t OUT_BASE_SIZE = 1024 * sizeof(char);
    const size_t MAX_ROW_LEN = 64;
    char *heading[] = {"MAC ADDR", "VPORT", "AGE"};

    size_t buf_size = OUT_BASE_SIZE;
    size_t out_len = 0;
    ssize_t row_len;
    int vport_idx;
    char *mac_addr_str;
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
        "%-23s%-23s%s\n===================================================\n",
        heading[0], heading[1], heading[2]
    );

    for (int i = 0; i < tbl_size; i++) {
        if (!tbl[i].used) {
            continue;
        }

        // Convert entry MAC address to string
        mac_addr_str = ether_ntoa((const struct ether_addr *)tbl[i].mac_addr);

        // Find vport index
        vport_idx = vsw_search_vport_idx(vports, vport_count, &tbl[i].vport->addr);
        if (vport_idx == -1) {
            continue;
        }

        // Construct row
        row_len = snprintf(row_buf, MAX_ROW_LEN, "%-23sVirt/%-18d%ld\n", mac_addr_str, vport_idx + 1, time(NULL) - tbl[i].created);

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


void vsw_add_mac_entry(vsw_entry_t *tbl, size_t tbl_size, uint8_t *mac_addr, vsw_vport_t *vport) {
    int old_idx = 0;
    time_t old_age = LONG_MAX;
    vsw_entry_t *free_slot = NULL;
    vsw_entry_t new = {
        .used = true,
        .vport = vport,
        .created = time(NULL)
    };
    memcpy(new.mac_addr, mac_addr, ETHER_ADDR_LEN);
    
    // Search for free slot in the table, while also looking for the oldest entry in the table
    // for replacement in case its fully occupied.
    for (int i = 0; i < tbl_size; i++) {
        // Unused slot found
        if (!tbl[i].used) {
            if (free_slot == NULL) {
                free_slot = &tbl[i];
            }
        }
        else {
            // If an entry for this MAC address already exists, just return
            if (memcmp(tbl[i].mac_addr, mac_addr, ETHER_ADDR_LEN) == 0) {
                return;
            } else if (tbl[i].created < old_age) {
                // Check if entry is the oldest
                old_age = tbl[i].created;
                old_idx = i;
            }
        }
    }

    if (free_slot != NULL) {
        // Insdert new entry in free slot
        *free_slot = new;
    } else {
        // Replace the oldest entry in the table
        tbl[old_idx] = new;
    }
}

vsw_entry_t *vsw_search_mac_entry(vsw_entry_t *tbl, size_t tbl_size, uint8_t *mac_addr) {
    for (int i = 0; i < tbl_size; i++) {
        if (!tbl[i].used) {
            continue;
        }
        if (memcmp(tbl[i].mac_addr, mac_addr, ETHER_ADDR_LEN) == 0) {
            return &tbl[i];
        }
    }
    return NULL;
}
