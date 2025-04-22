#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/neighbor.h"
#include "vswitch/core.h"


void vsw_connect_neighbor(vsw_state_t *vswitch, vsw_neighbor_t *peer) {
    uint8_t req = VSW_CTRL_MSG_JOIN;
    time_t last_req_time;
    ssize_t bytes_sent;
    
    for (int i = 0; i < VSW_JOIN_MAX_RETRIES; i++) {
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(&vswitch->lock);
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
        peer->status = VSW_TRUNK_PENDING;
        last_req_time = time(NULL); 
        // EXIT CRITICAL SECTION
        pthread_mutex_unlock(&vswitch->lock);

        // The response (if received) is handled in the main application thread.
        // Keep sending join requests if the the status of the neighborship doesn't change
        while (true) {
            // ENTER CRITICAL SECTION
            pthread_mutex_lock(&vswitch->lock);
            // Check if response was received from the peer
            if (peer->status != VSW_TRUNK_PENDING) {
                pthread_mutex_unlock(&vswitch->lock);
                return;
            }
            // No response from the peer yet and timeout expired - send another join request
            if (time(NULL) - last_req_time >= VSW_JOIN_RECV_TIMEOUT)  {
                pthread_mutex_unlock(&vswitch->lock);
                break;
            }
            // EXIT CRITICAL SECTION
            pthread_mutex_unlock(&vswitch->lock);
        }
    }
    // ENTER CRITICAL SECTION
    pthread_mutex_lock(&vswitch->lock);
    // No response received from peer vswitch after maximum retries
    peer->status = VSW_TRUNK_TIMEOUT;
    peer->used = false;
    // EXIT CRITICAL SECTION
    pthread_mutex_unlock(&vswitch->lock);
}


void *vsw_send_neighbor_heartbeats(void *_vswitch) {
    vsw_state_t *vswitch = (vsw_state_t *)_vswitch;
    uint8_t msg = VSW_CTRL_MSG_HEARTBEAT;
    ssize_t bytes_sent;

    // Send heartbeats in an infinite loop
    while (true) {
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(&vswitch->lock);
        for (int i = 0; i < VSW_NEIGHBOR_ARR_SIZE; i++) {
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
        pthread_mutex_unlock(&vswitch->lock);

        // Sleep until the next heartbeat
        sleep(VSW_HEARTBEAT_INTERVAL);
    }
}

vsw_action_t vsw_determine_switching_action(vsw_entry_t *mac_addr_tbl, uint8_t *mac_addr, vsw_vport_t *src_vport, vsw_entry_t **entry_ptr) {
    // L2 broadcast address
    const uint8_t L2_BRDCST_ADDR[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    // Check if frame is broadcast
    if (memcmp(mac_addr, L2_BRDCST_ADDR, ETHER_ADDR_LEN) == 0) {
        *entry_ptr = NULL;
        return VSW_ACTION_FLOOD;
    } else {
        // Search the MAC address table for the dest address
        *entry_ptr = vsw_search_mac_entry(mac_addr_tbl, VSW_MAC_ADDR_TBL_SIZE, mac_addr);
        // No entry flood - need to flood unknown unicast
        if (*entry_ptr == NULL) {
            return VSW_ACTION_FLOOD;
        }
        // Destination and source interfaces are the same, need to filter
        if ((*entry_ptr)->vport == src_vport) {
            return VSW_ACTION_FILTER;
        }
        // Valid entry found - forward traffic
        return VSW_ACTION_FORWARD;
    }
}


void vsw_flood_frame(int sockfd, vsw_vport_t *vport_arr, vsw_vport_t *src_vport, uint8_t *frame, ssize_t frame_size) {
    ssize_t bytes_sent;
    
    // Flood the frame through all interfaces except the source
    for (int i = 0; i < VSW_VPORT_ARR_SIZE; i++) {
        // Skip flooding the traffic trough the source interface
        if (!vport_arr[i].used || &vport_arr[i] == src_vport) {
            continue;
        }
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
        // Update destination interface statistics
        vport_arr[i].stats.tx_bytes += frame_size - VSW_CTRL_HDR_SIZE;
        vport_arr[i].stats.tx_packets++;
    }
}


void vsw_forward_frame(int sockfd, vsw_vport_t *vport, uint8_t *frame, ssize_t frame_size) {
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
    // Update destination interface statistics
    vport->stats.tx_bytes += frame_size - VSW_CTRL_HDR_SIZE;
    vport->stats.tx_packets++;
}

void *vsw_cleanup(void * _vswitch) {
    vsw_state_t *vswitch = (vsw_state_t *)_vswitch;
    int age;
    char log_buf[64];
    char addr_buf[INET_ADDRSTRLEN];

    // Continously look for objects to clean up
    while (true) {
        // ENTER CRITICAL SECTION
        pthread_mutex_lock(&vswitch->lock);
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
        pthread_mutex_unlock(&vswitch->lock);
    }
}