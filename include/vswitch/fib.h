#ifndef _VSW_FIB_H
#define _VSW_FIB_H

#include <stdbool.h>
#include <stdint.h>
#include <net/ethernet.h>
#include "vswitch/vport.h"

#define VSW_MAC_ADDR_TBL_SIZE 32

/*
Represents MAC address table entry. An array of these structs forms
the switch MAC address table.
*/
typedef struct {
    bool used;
    time_t created;
    uint8_t mac_addr[ETHER_ADDR_LEN];
    vport_t *vport;
} entry_t;

/*
Generates a string representation of the MAC address table and stores it in a heap-allocated buffer.
Returns the size of the string, not including the null terminator. The buffer should be freed at a later point.
*/
size_t vsw_mac_table_to_str(entry_t *tbl, vport_t *vports, char **buf);

/*
Adds a new entry to the MAC address table. If the table is full, the new entry
will override the oldest entry in the table.
*/
void vsw_add_mac_entry(entry_t *tbl, uint8_t *mac_addr, vport_t *vport);

/*
Searches for a matching entry in the MAC address table for
the given destination address. If an entry is found, returns a pointer to the element,
otherwise, returns NULL
*/
entry_t *vsw_search_mac_entry(entry_t *tbl, uint8_t *mac_addr);

#endif