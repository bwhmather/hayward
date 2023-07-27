#ifndef HWD_CONTROL_HWD_WORKSPACE_LIST_V1
#define HWD_CONTROL_HWD_WORKSPACE_LIST_V1

#include <wayland-server-core.h>

struct hwd_workspace_handle_v1 {
    struct hwd_workspace_manager_v1 *manager;
    struct wl_list resources; // wl_resource_get_link()
    struct wl_list link;

    struct {
        struct wl_signal request_focus;

        struct wl_signal destroy;
    } events;
};

struct hwd_workspace_manager_v1 {
    struct wl_global *global;

    struct wl_list resources;  // wl_resource_get_link()
    struct wl_list workspaces; // hwd_workspace_handle_v1::link

    struct {
        struct wl_signal destroy;
    } events;
};

struct hwd_workspace_manager_v1 *
hwd_workspace_manager_create(struct wl_display *display);

#endif
