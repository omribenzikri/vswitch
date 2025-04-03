#ifndef _VSW_COMMON_H
#define _VSW_COMMON_H 1

// Size of custom tunnel encapsulation in bytes
#define VSW_TUN_ENCAP_SIZE 1

// Different message types for tunnel header
typedef enum {
    VSW_TUN_MSG_TRFC, // Regular arbitrary traffic
    VSW_TUN_MSG_HRTBT, // Heartbeat
    VSW_TUN_MSG_JOIN, // Join request
    VSW_TUN_MSG_FREE, // OK to join - there is room on the vswitch
    VSW_TUN_MSG_FULL // Cannot join - vswitch is full 
} tun_msg_type_t;

#endif