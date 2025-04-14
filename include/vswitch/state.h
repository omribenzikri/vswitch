#ifndef _VSW_STATE_H
#define _VSW_STATE_H

#include <pthread.h>
#include "vswitch/vport.h"
#include "vswitch/fib.h"
#include "vswitch/trunk.h"

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
} vswitch_state_t;

#endif