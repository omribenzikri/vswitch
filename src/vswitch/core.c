#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "util/logging.h"
#include "vswitch/core.h"

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


void vsw_flood_frame(int sockfd, vport_t *vport_arr, vport_t *src_vport, uint8_t *frame, ssize_t frame_size) {
    ssize_t bytes_sent;
    
    // Flood the frame through all interfaces except the source
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        // Skip flooding the traffic trough the source interface
        if (!vport_arr[i].used || &vport_arr[i] == src_vport) {
            continue;
        }
        printf("Received frame from interface Virt/%d\n", i + 1);
        bytes_sent = sendto(
            sockfd,
            frame,
            frame_size,
            0,
            (struct sockaddr *)&vport_arr[i].addr,
            sizeof(struct sockaddr_in)
        );
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
    }
}


void vsw_forward_frame(int sockfd, vport_t *vport, uint8_t *frame, ssize_t frame_size) {
    ssize_t bytes_sent = sendto(
        sockfd,
        frame,
        frame_size,
        0,
        (struct sockaddr *)&(vport->addr),
        sizeof(struct sockaddr_in)
    );
    if (bytes_sent == -1) {
        log_sys_error();
        exit(EXIT_FAILURE);
    }
}

void *vsw_cleanup(void * _vswitch) {
    vswitch_state_t *vswitch = (vswitch_state_t *)_vswitch;
    int age;
    char log_buf[64];
    char addr_buf[INET_ADDRSTRLEN];

    // Continously look for objects to clean up
    while (true) {
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(vswitch->lock);
        for (int i = 0; i < VSW_MAC_ADDR_TBL_SIZE; i++) {
            if (!vswitch->mac_addr_tbl[i].used) {
                continue;
            }
            age = time(NULL) - vswitch->mac_addr_tbl[i].created;
            // Check if the entry is too old
            if (age > VSW_MAC_ENTRY_MAX_AGE) {
                // Set entry to unused
                vswitch->mac_addr_tbl[i].used = false;
            }
        }
        for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
            if (!vswitch->vports[i].used) {
                continue;
            }
            age = time(NULL) - vswitch->vports[i].last_seen;
            // Check if the vport is inactive
            if (age > VSW_VPORT_MAX_INACTIVE) {
                // Set slot to unused
                vswitch->vports[i].used = false;
                if (inet_ntop(AF_INET,  &vswitch->vports[i].addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
                    log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
                    exit(EXIT_FAILURE);
                }
                snprintf(log_buf, sizeof(log_buf), "Removed inactive vport from underlay address: %s", addr_buf);
                log_message(log_buf, LOG_INFO);
            }
        }
        // EXIT CRITICAL SECTION
        pthread_mutex_unlock(vswitch->lock);
    }
}