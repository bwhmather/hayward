#ifndef _HAYWARD_IPC_H
#define _HAYWARD_IPC_H

#define event_mask(ev) (1 << (ev & 0x7F))

typedef uint32_t ipc_command_type;

// i3 command types - see i3's I3_REPLY_TYPE constants
#define IPC_COMMAND 0
#define IPC_GET_WORKSPACES 1
#define IPC_SUBSCRIBE 2
#define IPC_GET_OUTPUTS 3
#define IPC_GET_TREE 4
#define IPC_GET_MARKS 5
#define IPC_GET_BAR_CONFIG 6
#define IPC_GET_VERSION 7
#define IPC_GET_BINDING_MODES 8
#define IPC_GET_CONFIG 9
#define IPC_SEND_TICK 10
#define IPC_SYNC 11
#define IPC_GET_BINDING_STATE 12

// hayward-specific command types
#define IPC_GET_INPUTS 100
#define IPC_GET_SEATS 101

// Events sent from hayward to clients. Events have the highest bits set.
#define IPC_EVENT_WORKSPACE 0x80000000
#define IPC_EVENT_OUTPUT 0x80000001
#define IPC_EVENT_MODE 0x80000002
#define IPC_EVENT_WINDOW 0x80000003
#define IPC_EVENT_BARCONFIG_UPDATE 0x80000004
#define IPC_EVENT_BINDING 0x80000005
#define IPC_EVENT_SHUTDOWN 0x80000006
#define IPC_EVENT_TICK 0x80000007

// hayward-specific event types
#define IPC_EVENT_BAR_STATE_UPDATE 0x80000014
#define IPC_EVENT_INPUT 0x80000015

#endif
