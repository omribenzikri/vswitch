#ifndef _VSW_COMMON_H
#define _VSW_COMMON_H 1

// Size of custom tunnel encapsulation in bytes
#define VSW_TUN_ENCAP_SIZE 1

// Different message types for tunnel header
#define VSW_TUN_ENCAP_TRFC 0 // Regular arbitrary traffic
#define VSW_TUN_ENCAP_HRTBT 1 // Heartbeat
#define VSW_TUN_ENCAP_OK 2 // OK to join - there is room on the vswitch
#define VSW_TUN_ENCAP_FULL 3 // Cannot join - vswitch is full

#endif