#ifndef _WMIIV_IPC_JSON_H
#define _WMIIV_IPC_JSON_H
#include <json.h>
#include "wmiiv/input/input-manager.h"

json_object *ipc_json_get_version(void);

json_object *ipc_json_get_binding_mode(void);

json_object *ipc_json_describe_disabled_output(struct wmiiv_output *o);
json_object *ipc_json_describe_node(struct wmiiv_node *node);
json_object *ipc_json_describe_node_recursive(struct wmiiv_node *node);
json_object *ipc_json_describe_input(struct wmiiv_input_device *device);
json_object *ipc_json_describe_seat(struct wmiiv_seat *seat);
json_object *ipc_json_describe_bar_config(struct bar_config *bar);

#endif
