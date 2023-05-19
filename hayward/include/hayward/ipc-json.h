#ifndef HAYWARD_IPC_JSON_H
#define HAYWARD_IPC_JSON_H

#include <json.h>

struct hayward_output;
struct hayward_input_device;
struct hayward_seat;
struct hayward_window;
struct hayward_column;
struct hayward_workspace;
struct hayward_root;

struct bar_config;

json_object *
ipc_json_get_version(void);

json_object *
ipc_json_get_binding_mode(void);

json_object *
ipc_json_describe_window(struct hayward_window *window);
json_object *
ipc_json_describe_output(struct hayward_output *output);
json_object *
ipc_json_describe_disabled_output(struct hayward_output *o);
json_object *
ipc_json_describe_workspace(struct hayward_workspace *workspace);
json_object *
ipc_json_describe_root(struct hayward_root *root);
json_object *
ipc_json_describe_input(struct hayward_input_device *device);
json_object *
ipc_json_describe_seat(struct hayward_seat *seat);
json_object *
ipc_json_describe_bar_config(struct bar_config *bar);

#endif
