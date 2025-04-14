#ifndef _VSW_CORE_H
#define _VSW_CORE_H

#include <pthread.h>
#include "vswitch/vport.h"
#include "vswitch/fib.h"
#include "vswitch/trunk.h"

#define VSW_MAC_ENTRY_MAX_AGE 300
#define VSW_VPORT_MAX_INACTIVE 10

/*
Represents the switch state data. This struct contains all
vital data structure for the operation of the vswitch. It is meant to be passable
to thread functions
*/
typedef struct {
    int sockfd;
    pthread_mutex_t *lock;
    vport_t *vports;
    neighbor_t *neighbors;
    entry_t *mac_addr_tbl;
} vswitch_t;

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

#endif