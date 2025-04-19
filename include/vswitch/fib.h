#ifndef _VSW_FIB_H
#define _VSW_FIB_H

#include <stdbool.h>
#include <stdint.h>
#include <net/ethernet.h>
#include "vswitch/vport.h"

/*
Represents a MAC address table entry. An array of these structs forms
the vswitch MAC address table.
*/
typedef struct {
    bool used;
    time_t created;
    uint8_t mac_addr[ETHER_ADDR_LEN];
    vsw_vport_t *vport;
} vsw_entry_t;

/*
Generates a string representation of the MAC address table and stores it in a heap-allocated buffer.
Returns the size of the string, not including the null terminator. The buffer should be freed at a later point.
*/
size_t vsw_mac_table_to_str(vsw_entry_t *tbl, vsw_vport_t *vports, char **buf);

/*
Adds a new entry to the MAC address table. If the table is full, the new entry
will override the oldest entry in the table.
*/
void vsw_add_mac_entry(vsw_entry_t *tbl, uint8_t *mac_addr, vsw_vport_t *vport);

/*
Searches for a matching entry in the MAC address table for
the given destination address. If an entry is found, returns a pointer to the struct,
otherwise, returns NULL
*/
vsw_entry_t *vsw_search_mac_entry(vsw_entry_t *tbl, uint8_t *mac_addr);

#endif