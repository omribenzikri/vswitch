#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include "proto.h"
#include "util/logging.h"
#include "vswitch/core.h"
#include "vswitch/trunk.h"
#include "vswitch/cli.h"


int main() {
    char log_buf[128];

    // Set up the MAC address table
    entry_t mac_addr_tbl[VSW_MAC_ADDR_TBL_SIZE];
    memset(mac_addr_tbl, 0, sizeof(mac_addr_tbl));
    snprintf(log_buf, sizeof(log_buf), "Allocated MAC address table of size: %d", VSW_MAC_ADDR_TBL_SIZE);
    log_message(log_buf, LOG_INFO);

    // Set up the interface table
    vport_t vport_arr[VSW_VPORT_ARR_SIZE];
    memset(vport_arr, 0, sizeof(vport_arr));
    snprintf(log_buf, sizeof(log_buf), "Allocated %d virtual interfaces.", VSW_VPORT_ARR_SIZE);
    log_message(log_buf, LOG_INFO);

    // Set up neighbors array
    neighbor_t neighbors_arr[VSW_NEIGHBORS_ARR_SIZE];
    memset(neighbors_arr, 0, sizeof(neighbors_arr));

    // Set up vswitch UDP socket
    struct sockaddr_in vsw_addr = {
        .sin_family = AF_INET,
        .sin_addr = {
            .s_addr = htonl(INADDR_ANY)
        },
        .sin_port = htons(VSW_UNDERLAY_PORT)
    };
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        log_sys_error();
        return EXIT_FAILURE;
    }

    // Bind vswitch UDP socket
    int ret = bind(sockfd, (struct sockaddr *)&vsw_addr, sizeof(vsw_addr));
    if (ret == -1) {
        log_sys_error();
        return EXIT_FAILURE;
    }
    snprintf(log_buf, sizeof(log_buf), "Bound underlay UDP socket to port: %d", VSW_UNDERLAY_PORT);
    log_message(log_buf, LOG_INFO);

    // Mutex lock used for thread synchronization
    pthread_mutex_t lock;
    pthread_mutex_init(&lock, NULL);

    // Group switch state data for passing into thread functions
    vswitch_state_t vswitch = {
        .sockfd = sockfd,
        .lock = &lock,
        .vports = vport_arr,
        .neighbors = neighbors_arr,
        .mac_addr_tbl = mac_addr_tbl
    };

    // Start cleanup thread
    pthread_t cleanup_thread_pid;
    pthread_create(&cleanup_thread_pid, NULL, &vsw_cleanup, &vswitch);
    snprintf(log_buf, sizeof(log_buf), "Started cleanup thread.");
    log_message(log_buf, LOG_DEBUG);

    // Start CLI server thread
    pthread_t cli_thread_pid;
    pthread_create(&cli_thread_pid, NULL, &vsw_cli_serve, &vswitch);
    snprintf(log_buf, sizeof(log_buf), "Started CLI server thread.");
    log_message(log_buf, LOG_DEBUG);

    // Start neighbor heartbeat thread
    pthread_t heartbeat_thread_pid;
    pthread_create(&heartbeat_thread_pid, NULL, &vsw_send_peer_heartbeats, &vswitch);
    snprintf(log_buf, sizeof(log_buf), "Started neighbor heartbeat thread.");
    log_message(log_buf, LOG_DEBUG);

    struct sockaddr_in vp_addr;
    socklen_t vp_addr_len = sizeof(vp_addr);
    uint8_t buf[ETHER_MAX_LEN + VSW_TUN_ENCAP_SIZE];
    neighbor_t *peer = NULL;

    while (true) {
        // Receive a frame from the socket
        ssize_t bytes_recvd = recvfrom(
            sockfd,
            buf,
            sizeof(buf),
            0,
            (struct sockaddr *)&vp_addr,
            &vp_addr_len
        );
        if (bytes_recvd == -1) {
            log_sys_error();
            return EXIT_FAILURE;
        }
        assert(bytes_recvd > 0);

        // ENTER CRITICAL SECTION
        pthread_mutex_lock(&lock);

        uint8_t frame_type = *buf;
        switch (frame_type)
        {
            case VSW_TUN_MSG_HRTBT:
                // Heartbeat received - search for the vport by address
                vport_t *vport = vsw_search_vport(vport_arr, &vp_addr);
                if (vport == NULL) {
                    pthread_mutex_unlock(&lock);
                    continue;
                }
                // Reset the last seen parameter
                vport->last_seen = time(NULL); 
                break;
            case VSW_TUN_MSG_JOIN:
                // Join message received - add a new virtual interface connection
                vport_t *new_vport = vsw_add_vport(vport_arr, &vp_addr);
                // Format an appropriate response
                uint8_t response = new_vport == NULL ? VSW_TUN_MSG_FULL : VSW_TUN_MSG_FREE;
                ssize_t bytes_sent = sendto(
                    sockfd,
                    &response,
                    sizeof(response),
                    0,
                    (struct sockaddr *)&vp_addr,
                    sizeof(struct sockaddr_in)
                );
                if (bytes_sent == -1) {
                    log_sys_error();
                    exit(EXIT_FAILURE);
                }

                char addr_buf[INET_ADDRSTRLEN];
                if (new_vport != NULL) {
                    if (inet_ntop(AF_INET, &new_vport->addr.sin_addr, addr_buf, sizeof(addr_buf)) == NULL) {
                        log_message("Failed to stringify vport IP address.", LOG_CRITICAL);
                        exit(EXIT_FAILURE);
                    }
                    snprintf(log_buf, sizeof(log_buf), "New vport connection from underlay address %s", addr_buf);
                    log_message(log_buf, LOG_INFO);
                }
                break;
            case VSW_TUN_MSG_FULL:
                peer = vsw_search_neighbor(neighbors_arr, &vp_addr);
                if (peer != NULL && peer->used) {
                    peer->status = TRUNK_PEER_FULL;
                }
                break;
            case VSW_TUN_MSG_FREE:
                peer = vsw_search_neighbor(neighbors_arr, &vp_addr);
                if (peer != NULL && peer->used) {
                    peer->status = TRUNK_ESTABLISHED;
                }
                break;
            case VSW_TUN_MSG_TRFC:
                
                // Strip the custom tunnel header
                uint8_t *ether_frame = buf + VSW_TUN_ENCAP_SIZE;
                size_t ether_frame_size = bytes_recvd - VSW_TUN_ENCAP_SIZE;
                
                // Parse ethernet header
                assert(ether_frame_size > ETHER_HDR_LEN);
                struct ether_header *hdr = (struct ether_header *)ether_frame;

                // Search for the interface by address
                vport_t *src_vport = vsw_search_vport(vport_arr, &vp_addr);
                if (src_vport == NULL) {
                    pthread_mutex_unlock(&lock);
                    continue;
                }

                // Update source interface statistics
                src_vport->stats.rx_bytes += ether_frame_size;
                src_vport->stats.rx_packets++;

                // Add new MAC address table entry
                vsw_add_mac_entry(mac_addr_tbl, hdr->ether_shost, src_vport);

                // Determine what to do with the frame
                entry_t *dst_entry;
                oper_mode_t mode = vsw_determine_oper_mode(mac_addr_tbl, hdr->ether_dhost, src_vport, &dst_entry);

                switch (mode)
                {
                    case OPER_FLOOD:
                        vsw_flood_frame(sockfd, vport_arr, src_vport, buf, bytes_recvd);
                        break;
                    case OPER_FORWARD:
                        vsw_forward_frame(sockfd, dst_entry->vport, buf, bytes_recvd);
                        break;
                    default:
                        src_vport->stats.drops++;
                        break;
                }
                break;
            default:
                break;
        }

        // EXIT CRITICAL SECTION
        pthread_mutex_unlock(&lock);
    }

    // Destroy lock
    pthread_mutex_destroy(&lock);

    // Close UDP sockets
    close(sockfd);

    return EXIT_SUCCESS;
}
