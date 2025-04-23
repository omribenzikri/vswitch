#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/neighbor.h"


size_t vsw_neighbor_table_to_str(vsw_neighbor_t *neighbors, size_t neighbor_count, vsw_vport_t *vports, size_t vport_count, char **buf) {
    const size_t OUT_BASE_SIZE = 1024 * sizeof(char);
    const size_t MAX_ROW_LEN = 64;
    const size_t MAX_VPORT_NAME_SIZE = 32;
    char *heading[] = {"UNDERLEY ADDRESS", "BIODIRECTIONAL", "LOCAL VPORT"};

    size_t buf_size = OUT_BASE_SIZE;
    size_t out_len = 0;
    ssize_t row_len;
    char addr_buf[INET_ADDRSTRLEN];
    char row_buf[MAX_ROW_LEN];

    int neighbor_vport_idx;
    char neighbor_vport_name[MAX_VPORT_NAME_SIZE];
    bool neighborship_two_way;
    
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
        "%-28s%-26s%s\n====================================================================\n",
        heading[0], heading[1], heading[2]
    );

    for (int i = 0; i < neighbor_count; i++) {
        if (!neighbors[i].used) {
            continue;
        }

        // Convert socket IP address to string
        if (inet_ntop(AF_INET, &neighbors[i].addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
            log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
            exit(EXIT_FAILURE);
        }

        // Search for a local interface of neighbor
        neighbor_vport_idx = vsw_search_vport_idx(vports, vport_count, &neighbors[i].addr);
        if (neighbor_vport_idx != -1) {
            snprintf(neighbor_vport_name, MAX_VPORT_NAME_SIZE, "Virt/%d", neighbor_vport_idx + 1);
            neighborship_two_way = true;
        } else {
            strncpy(neighbor_vport_name, "N/A", MAX_VPORT_NAME_SIZE);
            neighborship_two_way = false;
        }

        // Construct row
        row_len = snprintf(row_buf, MAX_ROW_LEN, "%-28s%-26s%s\n", addr_buf, neighborship_two_way ? "yes" : "no", neighbor_vport_name);

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


vsw_neighbor_t *vsw_add_neighbor(vsw_neighbor_t *neighbors, size_t count, struct sockaddr_in *addr) {
    vsw_neighbor_t new = {
        .used = true,
        .status = VSW_TRUNK_UINITIALIZED,
        .addr = *addr
    };
    for (int i = 0; i < count; i++) {
        // If neighbor already exists, just return
        if (addr->sin_addr.s_addr == neighbors[i].addr.sin_addr.s_addr && addr->sin_port == neighbors[i].addr.sin_port) {
            return &neighbors[i];
        }
        // Empty slot found, insert here
        if (!neighbors[i].used) {
            neighbors[i] = new;
            return &neighbors[i];
        }
    }
    return NULL;
}


vsw_neighbor_t *vsw_search_neighbor(vsw_neighbor_t *neighbors, size_t count, struct sockaddr_in *addr) {
    for (int i = 0; i < count; i++) {
        if (addr->sin_addr.s_addr == neighbors[i].addr.sin_addr.s_addr && addr->sin_port == neighbors[i].addr.sin_port) {
            return &neighbors[i];
        }
    }
    return NULL;
}

vsw_neighbor_t *vsw_remove_neighbor(vsw_neighbor_t *neighbors, size_t count, struct sockaddr_in *addr) {
    for (int i = 0; i < count; i++) {
        if (addr->sin_addr.s_addr == neighbors[i].addr.sin_addr.s_addr && addr->sin_port == neighbors[i].addr.sin_port) {
            neighbors[i].used = false;
            return &neighbors[i];
        }
    }
    return NULL;
}
