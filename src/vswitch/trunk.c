#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/core.h"
#include "vswitch/trunk.h"


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