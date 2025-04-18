#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/core.h"
#include "vswitch/trunk.h"


size_t vsw_neighbor_table_to_str(neighbor_t *neighbors, vport_t *vports, char **buf) {
    const size_t OUT_BASE_SIZE = 1024;
    const size_t MAX_ROW_SIZE = 64;
    const size_t MAX_VPORT_NAME_SIZE = 16;
    char heading[] = "UNDERLEY ADDRESS            BIODIRECTIONAL            LOCAL VPORT\n====================================================================\n";

    size_t buf_size = OUT_BASE_SIZE;
    size_t str_size = 0;
    ssize_t row_size;
    char addr_buf[INET_ADDRSTRLEN];
    char row_buf[MAX_ROW_SIZE];

    int neighbor_vport_idx;
    char neighbor_vport_name[MAX_VPORT_NAME_SIZE];
    bool neighborship_two_way;
    
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
        if (!neighbors[i].used) {
            continue;
        }

        // Convert socket IP address to string
        if (inet_ntop(AF_INET, &neighbors[i].addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
            log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
            exit(EXIT_FAILURE);
        }

        // Search for a local interface of neighbor
        neighbor_vport_idx = vsw_search_vport_idx(vports, &neighbors[i].addr);
        if (neighbor_vport_idx != -1) {
            snprintf(neighbor_vport_name, MAX_VPORT_NAME_SIZE, "Virt/%d", neighbor_vport_idx);
            neighborship_two_way = true;
        } else {
            strncpy(neighbor_vport_name, "N/A", MAX_VPORT_NAME_SIZE);
            neighborship_two_way = false;
        }

        // Construct row
        row_size = snprintf(row_buf, MAX_ROW_SIZE, "%-28s%-26s%s\n", addr_buf, neighborship_two_way ? "yes" : "no", neighbor_vport_name);

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


neighbor_t *vsw_add_neighbor(neighbor_t *neighbors, struct sockaddr_in *addr) {
    neighbor_t new = {
        .used = true,
        .status = TRUNK_UINITIALIZED,
        .addr = *addr
    };
    for (int i = 0; i < VSW_NEIGHBORS_ARR_SIZE; i++) {
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


neighbor_t *vsw_search_neighbor(neighbor_t *neighbors, struct sockaddr_in *addr) {
    for (int i = 0; i < VSW_NEIGHBORS_ARR_SIZE; i++) {
        if (addr->sin_addr.s_addr == neighbors[i].addr.sin_addr.s_addr && addr->sin_port == neighbors[i].addr.sin_port) {
            return &neighbors[i];
        }
    }
    return NULL;
}


void vsw_conn_peer(vswitch_state_t *vswitch, neighbor_t *peer) {
    uint8_t req = VSW_TUN_MSG_JOIN;
    time_t last_req_time;
    ssize_t bytes_sent;
    
    for (int i = 0; i < VSW_JOIN_MAX_RETRIES; i++) {
        // Send a join request to the peer vswitch
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(vswitch->lock);
        bytes_sent = sendto(
            vswitch->sockfd,
            &req,
            sizeof(req),
            0,
            (struct sockaddr *)&peer->addr,
            sizeof(struct sockaddr_in)
        );
        if (bytes_sent == -1) {
            log_sys_error();
            exit(EXIT_FAILURE);
        }
        // Update the neighborship status and wait
        peer->status = TRUNK_PENDING;
        last_req_time = time(NULL); 
        // EXIT CRITICAL SECTION
        pthread_mutex_unlock(vswitch->lock);

        // The response (if received) is handled in the main application thread.
        // Keep sending join requests if the the status of the neighborship doesn't change
        while (true) {
            // ENTER CRITICAL SECTION
            pthread_mutex_lock(vswitch->lock);
            // Check if response was received from the peer
            if (peer->status != TRUNK_PENDING) {
                pthread_mutex_unlock(vswitch->lock);
                return;
            }
            // No response from the peer yet and timeout expired - send another join request
            if (time(NULL) - last_req_time >= VSW_JOIN_RECV_TIMEOUT)  {
                pthread_mutex_unlock(vswitch->lock);
                break;
            }
            // EXIT CRITICAL SECTION
            pthread_mutex_unlock(vswitch->lock);
        }
    }
    // ENTER CRITICAL SECTION
    pthread_mutex_lock(vswitch->lock);
    // No response received from peer vswitch after maximum retries
    peer->status = TRUNK_TIMEOUT;
    peer->used = false;
    // EXIT CRITICAL SECTION
    pthread_mutex_unlock(vswitch->lock);
}


void *vsw_send_peer_heartbeats(void *_vswitch) {
    const vswitch_state_t *vswitch = (vswitch_state_t *)_vswitch;
    uint8_t msg = VSW_TUN_MSG_HRTBT;
    ssize_t bytes_sent;

    // Send heartbeats in an infinite loop
    while (true) {
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(vswitch->lock);
        for (int i = 0; i < VSW_NEIGHBORS_ARR_SIZE; i++) {
            if (!vswitch->neighbors[i].used) {
                continue;
            }
            bytes_sent = sendto(
                vswitch->sockfd,
                &msg,
                sizeof(msg),
                0,
                (struct sockaddr *)&vswitch->neighbors[i].addr,
                sizeof(struct sockaddr_in)
            );
            if (bytes_sent == -1) {
                log_sys_error();
                exit(EXIT_FAILURE);
            }
        }
        // EXIT CRITICAL SECTION
        pthread_mutex_unlock(vswitch->lock);

        // Sleep until the next heartbeat
        sleep(VSW_HEARTBEAT_INTERVAL);
    }
}