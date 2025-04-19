#ifndef _VSW_NEIGHBOR_H
#define _VSW_NEIGHBOR_H

#include <arpa/inet.h>
#include "vswitch/vport.h"

/*
Represents the different states of a neighborship with a peer vswitch
*/
typedef enum {
    VSW_TRUNK_UINITIALIZED, // No connection initialized
    VSW_TRUNK_PENDING,      // Join request sent, waiting for response from peer
    VSW_TRUNK_ESTABLISHED,  // Connection with peer established
    VSW_TRUNK_TIMEOUT,      // Connection to peer timed out after X retries
    VSW_TRUNK_PEER_FULL     // Peer has no open slots and cannot receive a new connection
} vsw_trunk_status_t;

/*
Represents a neighbor vswitch connection
*/
typedef struct {
    bool used;
    vsw_trunk_status_t status;
    struct sockaddr_in addr;
} vsw_neighbor_t;

/*
Generates a string representation of the neighbors table and stores it in a heap-allocated buffer.
Returns the size of the string, not including the null terminator. The buffer should be freed at a later point.
*/
size_t vsw_neighbor_table_to_str(vsw_neighbor_t *neighbors, vsw_vport_t *vports, char **buf);

/*
Adds a new neighbor to the array of neighbors. If there is room for a new neighbor,
returns a pointer to the added struct, otherwise, returns NULL.
*/
vsw_neighbor_t *vsw_add_neighbor(vsw_neighbor_t *neighbors, struct sockaddr_in *addr);

/*
Searches for a matching entry in the array of neighbors by socket pair. If a matching entry was found,
returns a pointer to the struct, otherwise, returns NULL.
*/
vsw_neighbor_t *vsw_search_neighbor(vsw_neighbor_t *neighbors, struct sockaddr_in *addr);

#endif