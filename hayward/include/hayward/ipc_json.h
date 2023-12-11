#ifndef HWD_IPC_JSON_H
#define HWD_IPC_JSON_H

#include <json.h>

struct hwd_output;
struct hwd_input_device;
struct hwd_seat;
struct hwd_window;
struct hwd_column;
struct hwd_workspace;
struct hwd_root;

struct bar_config;

json_object *
ipc_json_get_version(void);

json_object *
ipc_json_get_binding_mode(void);

json_object *
ipc_json_describe_window(struct hwd_window *window);
json_object *
ipc_json_describe_workspace(struct hwd_workspace *workspace);
json_object *
ipc_json_describe_root(struct hwd_root *root);
json_object *
ipc_json_describe_input(struct hwd_input_device *device);
json_object *
ipc_json_describe_seat(struct hwd_seat *seat);
json_object *
ipc_json_describe_bar_config(struct bar_config *bar);

#endif
