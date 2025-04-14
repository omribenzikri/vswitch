#ifndef _VSW_TRUNK_H
#define _VSW_TRUNK_H

#include <arpa/inet.h>

#define VSW_NEIGHBORS_ARR_SIZE 4
#define VSW_JOIN_MAX_RETRIES 5
#define VSW_JOIN_RECV_TIMEOUT 2
#define VSW_HEARTBEAT_INTERVAL 2

/*
Represents the different states of a neighborship to a peer vswitch
*/
typedef enum {
    TRUNK_UINITIALIZED, // No connection initialized
    TRUNK_PENDING, // Join request sent, waiting for response from peer
    TRUNK_ESTABLISHED, // Connection with peer established
    TRUNK_TIMEOUT, // Connection to peer timed out after X retries
    TRUNK_PEER_FULL // Peer has no open slots and cannot receive a new connection
} trunk_status_t;

/*
Represents a neighbor vswitch connection
*/
typedef struct {
    bool used;
    trunk_status_t status;
    struct sockaddr_in addr;
} neighbor_t;

/*
Adds a new neighbor to the array of neighbors. If there is room for a new neighbor,
returns a pointer to the added struct, otherwise, returns NULL.
*/
neighbor_t *vsw_add_neighbor(neighbor_t *neighbors, struct sockaddr_in *addr);

/*
Searches for a matching entry in the array of neighbors by socket pair. If a matching entry was found,
returns a pointer to the struct, otherwise, returns NULL.
*/
neighbor_t *vsw_search_neighbor(neighbor_t *neighbors, struct sockaddr_in *addr);

/*
Continuously sends JOIN requests to the given peer until its neighborship status is updated.
The status is updated in the main application thread where all traffic is received and processed.
If the maxmimum number of retries was exceeded and the neighborship is still in a pending status,
the function deactivates the neighbor and sets its status accordingly.
*/
void vsw_conn_peer(int sockfd, neighbor_t *peer);

/*
Continuously sends heartbeats to active peers to let them know that the vswitch is still alive.
This function is meant to run on a separate thread.
*/
void *vsw_send_peer_heartbeats(void *_vswitch);

#endif