#ifndef HWD_CONTROL_HWD_WORKSPACE_MANAGEMENT_V1_H
#define HWD_CONTROL_HWD_WORKSPACE_MANAGEMENT_V1_H

#include <wayland-server-core.h>
#include <wayland-util.h>

struct hwd_workspace_handle_v1 {
    struct hwd_workspace_manager_v1 *manager;
    struct wl_list resources; // wl_resource_get_link()
    struct wl_list link;

    char *name;

    struct {
        // struct hwd_workspace_handle_v1_focus_event
        struct wl_signal request_focus;

        struct wl_signal destroy;
    } events;
};

struct hwd_workspace_handle_v1_focus_event {
    struct hwd_workspace_handle_v1 *workspace;
};

struct hwd_workspace_handle_v1 *
hwd_workspace_handle_v1_create(struct hwd_workspace_manager_v1 *manager);

void
hwd_workspace_handle_v1_destroy(struct hwd_workspace_handle_v1 *workspace);

void
hwd_workspace_handle_v1_set_name(struct hwd_workspace_handle_v1 *workspace, const char *name);

struct hwd_workspace_manager_v1 {
    struct wl_event_loop *event_loop;
    struct wl_global *global;

    struct wl_list resources;  // wl_resource_get_link()
    struct wl_list workspaces; // hwd_workspace_handle_v1::link

    struct wl_event_source *idle_source;

    struct {
        struct wl_signal destroy;
    } events;
};

struct hwd_workspace_manager_v1 *
hwd_workspace_manager_v1_create(struct wl_display *display);

#endif
