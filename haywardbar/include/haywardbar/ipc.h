#ifndef _HAYWARDBAR_IPC_H
#define _HAYWARDBAR_IPC_H
#include <stdbool.h>

#include <haywardbar/bar.h>
#include <haywardbar/config.h>

bool
ipc_initialize(struct haywardbar *bar);
bool
handle_ipc_readable(struct haywardbar *bar);
bool
ipc_get_workspaces(struct haywardbar *bar);
void
ipc_send_workspace_command(struct haywardbar *bar, const char *ws);
void
ipc_execute_binding(struct haywardbar *bar, struct haywardbar_binding *bind);

#endif
