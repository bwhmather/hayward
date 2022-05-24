#ifndef _SWAYBAR_IPC_H
#define _SWAYBAR_IPC_H
#include <stdbool.h>
#include "wmiivbar/bar.h"

bool ipc_initialize(struct wmiivbar *bar);
bool handle_ipc_readable(struct wmiivbar *bar);
bool ipc_get_workspaces(struct wmiivbar *bar);
void ipc_send_workspace_command(struct wmiivbar *bar, const char *ws);
void ipc_execute_binding(struct wmiivbar *bar, struct wmiivbar_binding *bind);

#endif
