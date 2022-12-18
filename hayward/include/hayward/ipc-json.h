#ifndef _HAYWARD_IPC_JSON_H
#define _HAYWARD_IPC_JSON_H
#include <json.h>

#include "hayward/input/input-manager.h"

json_object *
ipc_json_get_version(void);

json_object *
ipc_json_get_binding_mode(void);

json_object *
ipc_json_describe_disabled_output(struct hayward_output *o);
json_object *
ipc_json_describe_node(struct hayward_node *node);
json_object *
ipc_json_describe_node_recursive(struct hayward_node *node);
json_object *
ipc_json_describe_input(struct hayward_input_device *device);
json_object *
ipc_json_describe_seat(struct hayward_seat *seat);
json_object *
ipc_json_describe_bar_config(struct bar_config *bar);

#endif
