#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/control/hwd_workspace_management_v1.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>

#include <hayward-common/log.h>

#include <hwd-workspace-management-unstable-v1-protocol.h>

static void
manager_set_dirty(struct hwd_workspace_manager_v1 *manager);

static void
workspace_handle_focus(struct wl_client *client, struct wl_resource *resource);

static const struct hwd_workspace_handle_v1_interface workspace_handle_impl = {
    .focus = workspace_handle_focus,
};

static struct hwd_workspace_handle_v1 *
workspace_handle_from_resource(struct wl_resource *resource) {
    hwd_assert(
        wl_resource_instance_of(
            resource, &hwd_workspace_handle_v1_interface, &workspace_handle_impl
        ),
        "Invalid instance"
    );

    return wl_resource_get_user_data(resource);
}

static void
workspace_handle_focus(struct wl_client *client, struct wl_resource *resource) {
    struct hwd_workspace_handle_v1 *workspace = workspace_handle_from_resource(resource);
    if (workspace == NULL) {
        return;
    }

    struct hwd_workspace_handle_v1_focus_event event = {.workspace = workspace};
    wl_signal_emit_mutable(&workspace->events.request_focus, &event);
}

static void
workspace_resource_destroy(struct wl_resource *resource) {
    wl_list_remove(wl_resource_get_link(resource));
}

static struct wl_resource *
create_workspace_resource_for_resource(
    struct hwd_workspace_handle_v1 *workspace, struct wl_resource *manager_resource
) {
    struct wl_client *client = wl_resource_get_client(manager_resource);
    struct wl_resource *resource = wl_resource_create(
        client, &hwd_workspace_handle_v1_interface, wl_resource_get_version(manager_resource), 0
    );
    if (!resource) {
        wl_client_post_no_memory(client);
        return NULL;
    }

    wl_resource_set_implementation(
        resource, &workspace_handle_impl, workspace, workspace_resource_destroy
    );

    wl_list_insert(&workspace->resources, wl_resource_get_link(resource));
    hwd_workspace_manager_v1_send_workspace(manager_resource, resource);

    if (workspace->name != NULL) {
        hwd_workspace_handle_v1_send_name(resource, workspace->name);
    }

    return resource;
}

struct hwd_workspace_handle_v1 *
hwd_workspace_handle_v1_create(struct hwd_workspace_manager_v1 *manager) {
    hwd_assert(manager != NULL, "Expected workspace manager");

    struct hwd_workspace_handle_v1 *workspace = calloc(1, sizeof(struct hwd_workspace_handle_v1));
    if (workspace == NULL) {
        return NULL;
    }

    wl_list_insert(&manager->workspaces, &workspace->link);
    workspace->manager = manager;

    wl_list_init(&workspace->resources);

    wl_signal_init(&workspace->events.request_focus);
    wl_signal_init(&workspace->events.destroy);

    struct wl_resource *manager_resource, *tmp;
    wl_resource_for_each_safe(manager_resource, tmp, &manager->resources) {
        create_workspace_resource_for_resource(workspace, manager_resource);
    }

    manager_set_dirty(manager);

    return workspace;
}

void
hwd_workspace_handle_v1_destroy(struct hwd_workspace_handle_v1 *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace handle");

    wl_signal_emit_mutable(&workspace->events.destroy, workspace);

    struct wl_resource *resource, *tmp;
    wl_resource_for_each_safe(resource, tmp, &workspace->resources) {
        hwd_workspace_handle_v1_send_closed(resource);
        wl_resource_set_user_data(resource, NULL);
        wl_list_remove(wl_resource_get_link(resource));
        wl_list_init(wl_resource_get_link(resource));
    }

    free(workspace->name);
    free(workspace);
}

void
hwd_workspace_handle_v1_set_name(struct hwd_workspace_handle_v1 *workspace, const char *name) {
    workspace->name = strdup(name);
    hwd_assert(workspace->name != NULL, "Could not allocate memory for name");

    struct wl_resource *resource;
    wl_resource_for_each(resource, &workspace->resources) {
        hwd_workspace_handle_v1_send_name(resource, workspace->name);
    }

    manager_set_dirty(workspace->manager);
}

void
hwd_workspace_handle_v1_set_focused(struct hwd_workspace_handle_v1 *workspace, bool focused) {
    if (focused == workspace->focused) {
        return;
    }
    workspace->focused = focused;

    struct wl_resource *resource;
    wl_resource_for_each(resource, &workspace->resources) {
        hwd_workspace_handle_v1_send_focused(resource, focused);
    }

    manager_set_dirty(workspace->manager);
}

static void
manager_handle_stop(struct wl_client *client, struct wl_resource *resource);

static const struct hwd_workspace_manager_v1_interface workspace_manager_impl = {
    .stop = manager_handle_stop,
};

static void
manager_handle_stop(struct wl_client *client, struct wl_resource *resource) {
    hwd_assert(
        wl_resource_instance_of(
            resource, &hwd_workspace_manager_v1_interface, &workspace_manager_impl
        ),
        "Invalid instance"
    );

    hwd_workspace_manager_v1_send_finished(resource);
    wl_resource_destroy(resource);
}

static void
manager_handle_resource_destroy(struct wl_resource *resource) {
    wl_list_remove(wl_resource_get_link(resource));
}

static void
manager_handle_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
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

    struct hwd_workspace_handle_v1 *workspace, *tmp;
    wl_list_for_each_safe(workspace, tmp, &manager->workspaces, link) {
        create_workspace_resource_for_resource(workspace, resource);
    }

    hwd_workspace_manager_v1_send_done(resource);
}

static void
manager_idle_send_done(void *data) {
    struct hwd_workspace_manager_v1 *manager = data;
    struct wl_resource *resource;
    wl_resource_for_each(resource, &manager->resources) {
        hwd_workspace_manager_v1_send_done(resource);
    }
    manager->idle_source = NULL;
}

static void
manager_set_dirty(struct hwd_workspace_manager_v1 *manager) {
    if (manager->idle_source != NULL) {
        return;
    }
    manager->idle_source =
        wl_event_loop_add_idle(manager->event_loop, manager_idle_send_done, manager);
}

struct hwd_workspace_manager_v1 *
hwd_workspace_manager_v1_create(struct wl_display *display) {
    struct hwd_workspace_manager_v1 *manager = calloc(1, sizeof(struct hwd_workspace_manager_v1));
    if (manager == NULL) {
        return NULL;
    }

    manager->event_loop = wl_display_get_event_loop(display);
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
