#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/control/hwd_workspace_management_v1.h"

#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <hayward-common/log.h>

#include <hwd-workspace-management-unstable-v1-protocol.h>

static void
manager_handle_stop(struct wl_client *client, struct wl_resource *resource) {}

static const struct hwd_workspace_manager_v1_interface workspace_manager_impl = {
    .stop = manager_handle_stop,
};

static void
manager_handle_resource_destroy(struct wl_resource *resource) {
    hwd_log(HWD_ERROR, "UNBIND!");
    wl_list_remove(wl_resource_get_link(resource));
}

static void
manager_handle_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    hwd_log(HWD_ERROR, "BIND!");
    struct hwd_workspace_manager_v1 *manager = data;
    struct wl_resource *resource =
        wl_resource_create(client, &hwd_workspace_manager_v1_interface, version, id);
    if (resource == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(
        resource, &workspace_manager_impl, manager, manager_handle_resource_destroy
    );

    wl_list_insert(&manager->resources, wl_resource_get_link(resource));
}

struct hwd_workspace_manager_v1 *
hwd_workspace_manager_create(struct wl_display *display) {
    hwd_log(HWD_ERROR, "CREATE!");
    struct hwd_workspace_manager_v1 *manager = calloc(1, sizeof(struct hwd_workspace_manager_v1));
    if (manager == NULL) {
        return NULL;
    }

    manager->global = wl_global_create(
        display, &hwd_workspace_manager_v1_interface, 1, manager, manager_handle_bind
    );
    if (manager->global == NULL) {
        free(manager);
        return NULL;
    }

    wl_signal_init(&manager->events.destroy);
    wl_list_init(&manager->resources);
    wl_list_init(&manager->workspaces);

    return manager;
}
