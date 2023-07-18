#ifndef HWD_IPC_SERVER_H
#define HWD_IPC_SERVER_H

#include <stdbool.h>

struct bar_config;
struct hwd_binding;
struct hwd_workspace;
struct hwd_input_device;
struct hwd_window;
struct hwd_server;

void
ipc_init(struct hwd_server *server);

void
ipc_event_workspace(struct hwd_workspace *old, struct hwd_workspace *new, const char *change);
void
ipc_event_window(struct hwd_window *window, const char *change);
void
ipc_event_barconfig_update(struct bar_config *bar);
void
ipc_event_bar_state_update(struct bar_config *bar);
void
ipc_event_mode(const char *mode, bool pango);
void
ipc_event_shutdown(const char *reason);
void
ipc_event_binding(struct hwd_binding *binding);
void
ipc_event_input(const char *change, struct hwd_input_device *device);

#endif
