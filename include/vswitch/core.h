#ifndef _VSW_CORE_H
#define _VSW_CORE_H

#include "vswitch/state.h"

#define VSW_MAC_ENTRY_MAX_AGE 300
#define VSW_VPORT_MAX_INACTIVE 10

/*
Different opertional modes of a switch
*/
typedef enum {
    OPER_FLOOD,
    OPER_FORWARD,
    OPER_FILTER
} oper_mode_t;

/*
Looks up the given destination MAC in the table in order to find a matching entry.
If an entry was found, the provided entry pointer will be updated to point to the entry struct, otherwise,
it will point to NULL. Returns the appropriate operational mode (forward, flood, filter).
*/
oper_mode_t vsw_determine_oper_mode(entry_t *mac_addr_tbl, uint8_t *mac_addr, vport_t *src_vport, entry_t **entry_ptr);

/*
Floods received ethernet frame through all connected interfaces, except for the source interface.
"Flooding" in this context means forwarding the frame through the underlay UDP socket to all connected vports except for the source.
*/
void vsw_flood_frame(int sockfd, vport_t *vport_arr, vport_t *src_vport, uint8_t *frame, ssize_t frame_size);

/*
Forwards received ethernet frame through the provided virtual interface.
*/
void vsw_forward_frame(int sockfd, vport_t *vport, uint8_t *frame, ssize_t frame_size);

/*
Removes inactive vports and deletes old MAC addres table entries.
This function should run in its own thread.
*/
void *vsw_cleanup(void * _vswitch);

/*
Continuously sends JOIN requests to the given peer until its neighborship status is updated.
The status is updated in the main application thread where all traffic is received and processed.
If the maxmimum number of retries was exceeded and the neighborship is still in a pending status,
the function deactivates the neighbor and sets its status accordingly.
*/
void vsw_conn_peer(vswitch_state_t *vswitch, neighbor_t *peer);

/*
Continuously sends heartbeats to active peers to let them know that the vswitch is still alive.
This function is meant to run on a separate thread.
*/
void *vsw_send_peer_heartbeats(void *_vswitch);

#endif