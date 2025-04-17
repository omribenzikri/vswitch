#ifndef _VSW_STATS_H
#define _VSW_STATS_H

#include <stdint.h>

/*
Virtual interface statistics.
*/
typedef struct {
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t tx_packets;
    uint64_t drops;
} vport_stats_t;

#endif