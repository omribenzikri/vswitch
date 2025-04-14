#ifndef _VSW_VPORT_H
#define _VSW_VPORT_H

#include <stdbool.h>
#include <arpa/inet.h>

#define VSW_VPORT_ARR_SIZE 24

/*
Represents a virtual interface
*/
typedef struct {
    bool used;
    time_t last_seen;
    struct sockaddr_in addr;
} vport_t;

/*
Generates a string representation of the vport able and stores it in a heap-allocated buffer.
Returns the size of the string, not including the null terminator. The buffer should be freed at a later point.
*/
size_t vsw_vport_table_to_str(vport_t *vports, char **buf);

/*
Adds a new virtual interface to the array of interfaces. If there is room for a new connection,
returns a pointer to the struct. Otherwise, returns NULL.
*/
vport_t *vsw_add_vport(vport_t *vports, struct sockaddr_in *addr);

/*
Searches for a virtual interface in the interfaces array by socket pair.
If found - returns a pointer to the struct, otherwise, returns NULL
*/
vport_t *vsw_search_vport(vport_t *vports, struct sockaddr_in *vport_addr);

#endif