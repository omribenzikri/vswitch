#ifndef _VSW_PROTO_H
#define _VSW_PROTO_H

/*
Listen UDP port for vswitch
*/
#define VSW_UNDERLAY_PORT 9200

/*
Size of custom control header in bytes
*/
#define VSW_CTRL_HDR_SIZE 1

/*
Different types of control messages
*/
typedef enum {
    VSW_CTRL_MSG_TRAFFIC,   // Arbitrary traffic
    VSW_CTRL_MSG_HEARTBEAT, // Heartbeat
    VSW_CTRL_MSG_JOIN,      // Join request
    VSW_CTRL_MSG_FREE,      // OK to join - there's enough room on the vswitch
    VSW_CTRL_MSG_FULL       // Cannot join - there's no room on the vswitch
} vsw_ctl_msg_t;

#endif