#ifndef _WMIIV_IPC_SERVER_H
#define _WMIIV_IPC_SERVER_H
#include <sys/socket.h>
#include "wmiiv/config.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/tree/container.h"
#include "ipc.h"

struct wmiiv_server;

void ipc_init(struct wmiiv_server *server);

struct sockaddr_un *ipc_user_sockaddr(void);

void ipc_event_workspace(struct wmiiv_workspace *old,
		struct wmiiv_workspace *new, const char *change);
void ipc_event_window(struct wmiiv_container *window, const char *change);
void ipc_event_barconfig_update(struct bar_config *bar);
void ipc_event_bar_state_update(struct bar_config *bar);
void ipc_event_mode(const char *mode, bool pango);
void ipc_event_shutdown(const char *reason);
void ipc_event_binding(struct wmiiv_binding *binding);
void ipc_event_input(const char *change, struct wmiiv_input_device *device);

#endif
