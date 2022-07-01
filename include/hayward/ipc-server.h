#ifndef _HAYWARD_IPC_SERVER_H
#define _HAYWARD_IPC_SERVER_H
#include <sys/socket.h>
#include "hayward/config.h"
#include "hayward/input/input-manager.h"
#include "hayward/tree/window.h"
#include "ipc.h"

struct hayward_server;

void ipc_init(struct hayward_server *server);

struct sockaddr_un *ipc_user_sockaddr(void);

void ipc_event_workspace(struct hayward_workspace *old,
		struct hayward_workspace *new, const char *change);
void ipc_event_window(struct hayward_window *window, const char *change);
void ipc_event_barconfig_update(struct bar_config *bar);
void ipc_event_bar_state_update(struct bar_config *bar);
void ipc_event_mode(const char *mode, bool pango);
void ipc_event_shutdown(const char *reason);
void ipc_event_binding(struct hayward_binding *binding);
void ipc_event_input(const char *change, struct hayward_input_device *device);

#endif
