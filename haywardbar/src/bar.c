#define _POSIX_C_SOURCE 200809L
#include "haywardbar/bar.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include <haywardbar/config.h>
#include <haywardbar/i3bar.h>
#include <haywardbar/input.h>
#include <haywardbar/ipc.h>
#include <haywardbar/render.h>
#include <haywardbar/status_line.h>

#include <config.h>
#if HAVE_TRAY
#include <haywardbar/tray/tray.h>
#endif
#include <hayward-client/pool-buffer.h>
#include <hayward-common/ipc-client.h>
#include <hayward-common/list.h>
#include <hayward-common/log.h>
#include <hayward-common/loop.h>

#include <hwd-workspace-management-unstable-v1-client-protocol.h>
#include <wlr-layer-shell-unstable-v1-client-protocol.h>
#include <xdg-output-unstable-v1-client-protocol.h>

static void
haywardbar_workspace_free(struct haywardbar_workspace *workspace) {
    wl_list_remove(&workspace->link);
    free(workspace->name);
    free(workspace);
}

static void
haywardbar_output_free(struct haywardbar_output *output) {
    if (!output) {
        return;
    }
    hwd_log(HWD_DEBUG, "Removing output %s", output->name);
    if (output->layer_surface != NULL) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
    }
    if (output->surface != NULL) {
        wl_surface_destroy(output->surface);
    }
    wl_output_destroy(output->output);
    destroy_buffer(&output->buffers[0]);
    destroy_buffer(&output->buffers[1]);
    free_hotspots(&output->hotspots);
    wl_list_remove(&output->link);
    free(output->name);
    free(output->identifier);
    free(output);
}

static void
set_output_dirty(struct haywardbar_output *output) {
    if (output->frame_scheduled) {
        output->dirty = true;
    } else if (output->surface) {
        render_frame(output);
    }
}

static void
layer_surface_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial, uint32_t width,
    uint32_t height
) {
    struct haywardbar_output *output = data;
    output->width = width;
    output->height = height;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    set_output_dirty(output);
}

static void
layer_surface_closed(void *_output, struct zwlr_layer_surface_v1 *surface) {
    struct haywardbar_output *output = _output;
    haywardbar_output_free(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void
add_layer_surface(struct haywardbar_output *output) {
    if (output->layer_surface) {
        return;
    }
    struct haywardbar *bar = output->bar;

    struct haywardbar_config *config = bar->config;
    bool hidden = strcmp(config->mode, "hide") == 0;
    bool overlay = !hidden && strcmp(config->mode, "overlay") == 0;
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        bar->layer_shell, output->surface, output->output,
        hidden || overlay ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY : ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        "panel"
    );
    assert(output->layer_surface);
    zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);

    if (overlay) {
        // Empty input region
        struct wl_region *region = wl_compositor_create_region(bar->compositor);
        wl_surface_set_input_region(output->surface, region);
        wl_region_destroy(region);
    }

    zwlr_layer_surface_v1_set_anchor(output->layer_surface, config->position);
    if (hidden || overlay) {
        zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    }
}

void
destroy_layer_surface(struct haywardbar_output *output) {
    if (!output->layer_surface) {
        return;
    }
    zwlr_layer_surface_v1_destroy(output->layer_surface);
    wl_surface_attach(output->surface, NULL, 0, 0); // detach buffer
    output->layer_surface = NULL;
    output->width = 0;
    output->frame_scheduled = false;
}

void
set_bar_dirty(struct haywardbar *bar) {
    struct haywardbar_output *output;
    wl_list_for_each(output, &bar->outputs, link) { set_output_dirty(output); }
}

bool
determine_bar_visibility(struct haywardbar *bar, bool moving_layer) {
    struct haywardbar_config *config = bar->config;
    bool visible =
        !(strcmp(config->mode, "invisible") == 0 ||
          (strcmp(config->mode, config->hidden_state) == 0 // both "hide"
           && !bar->visible_by_modifier && !bar->visible_by_urgency && !bar->visible_by_mode));

    // Create/destroy layer surfaces as needed
    struct haywardbar_output *output;
    wl_list_for_each(output, &bar->outputs, link) {
        // When moving to a different layer, we need to destroy and re-create
        // the layer surface
        if (!visible || moving_layer) {
            destroy_layer_surface(output);
        }

        if (visible) {
            add_layer_surface(output);
        }
    }
    set_bar_dirty(bar);

    if (visible != bar->visible) {
        bar->visible = visible;

        if (bar->status) {
            hwd_log(HWD_DEBUG, "Sending %s signal to status command", visible ? "cont" : "stop");
            kill(-bar->status->pid, visible ? bar->status->cont_signal : bar->status->stop_signal);
        }
    }

    return visible;
}

static bool
bar_uses_output(struct haywardbar_output *output) {
    if (wl_list_empty(&output->bar->config->outputs)) {
        return true;
    }
    char *identifier = output->identifier;
    struct config_output *coutput;
    wl_list_for_each(coutput, &output->bar->config->outputs, link) {
        if (strcmp(coutput->name, output->name) == 0 ||
            (identifier && strcmp(coutput->name, identifier) == 0)) {
            return true;
        }
    }
    return false;
}

static void
output_geometry(
    void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t width_mm,
    int32_t height_mm, int32_t subpixel, const char *make, const char *model, int32_t transform
) {
    struct haywardbar_output *output = data;
    output->subpixel = subpixel;
}

static void
output_mode(
    void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height,
    int32_t refresh
) {
    // Who cares
}

static void
output_done(void *data, struct wl_output *wl_output) {
    struct haywardbar_output *output = data;
    set_output_dirty(output);
}

static void
output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    struct haywardbar_output *output = data;
    output->scale = factor;

    bool render = false;
    struct haywardbar_seat *seat;
    wl_list_for_each(seat, &output->bar->seats, link) {
        if (output == seat->pointer.current) {
            update_cursor(seat);
            render = true;
        }
    };
    if (render) {
        render_frame(output);
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

static void
xdg_output_handle_logical_position(
    void *data, struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y
) {
    struct haywardbar_output *output = data;
    output->output_x = x;
    output->output_y = y;
}

static void
xdg_output_handle_logical_size(
    void *data, struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height
) {
    struct haywardbar_output *output = data;
    output->output_height = height;
    output->output_width = width;
}

static void
xdg_output_handle_done(void *data, struct zxdg_output_v1 *xdg_output) {
    struct haywardbar_output *output = data;
    struct haywardbar *bar = output->bar;

    if (!wl_list_empty(&output->link)) {
        return;
    }

    assert(output->name != NULL);
    if (!bar_uses_output(output)) {
        wl_list_remove(&output->link);
        wl_list_insert(&bar->unused_outputs, &output->link);
        return;
    }

    wl_list_remove(&output->link);
    wl_list_insert(&bar->outputs, &output->link);

    output->surface = wl_compositor_create_surface(bar->compositor);
    assert(output->surface);

    determine_bar_visibility(bar, false);
}

static void
xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
    struct haywardbar_output *output = data;
    free(output->name);
    output->name = strdup(name);
}

static void
xdg_output_handle_description(
    void *data, struct zxdg_output_v1 *xdg_output, const char *description
) {
    // wlroots currently sets the description to `make model serial (name)`
    // If this changes in the future, this will need to be modified.
    struct haywardbar_output *output = data;
    free(output->identifier);
    output->identifier = NULL;
    char *paren = strrchr(description, '(');
    if (paren) {
        size_t length = paren - description;
        output->identifier = malloc(length);
        if (!output->identifier) {
            hwd_log(HWD_ERROR, "Failed to allocate output identifier");
            return;
        }
        strncpy(output->identifier, description, length);
        output->identifier[length - 1] = '\0';
    }
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
    .logical_position = xdg_output_handle_logical_position,
    .logical_size = xdg_output_handle_logical_size,
    .done = xdg_output_handle_done,
    .name = xdg_output_handle_name,
    .description = xdg_output_handle_description,
};

static void
hwd_workspace_handle_closed(void *data, struct hwd_workspace_handle_v1 *workspace_handle) {
    struct haywardbar_workspace *workspace = data;
    struct haywardbar *bar = workspace->bar;

    hwd_workspace_handle_v1_set_user_data(workspace_handle, NULL);

    haywardbar_workspace_free(workspace);
    determine_bar_visibility(bar, false);
}

void
hwd_workspace_handle_name(
    void *data, struct hwd_workspace_handle_v1 *workspace_handle, const char *name
) {
    struct haywardbar_workspace *workspace = data;
    free(workspace->name);
    workspace->name = strdup(name);
}

void
hwd_workspace_handle_focused(
    void *data, struct hwd_workspace_handle_v1 *workspace_handle, int focused
) {
    struct haywardbar_workspace *workspace = data;
    workspace->focused = focused != 0;
}

static const struct hwd_workspace_handle_v1_listener hwd_workspace_handle_listener = {
    .closed = hwd_workspace_handle_closed,
    .name = hwd_workspace_handle_name,
    .focused = hwd_workspace_handle_focused};

static void
hwd_workspace_manager_handle_workspace(
    void *data, struct hwd_workspace_manager_v1 *hwd_workspace_manager_v1,
    struct hwd_workspace_handle_v1 *hwd_workspace_handle_v1
) {
    struct haywardbar *bar = hwd_workspace_manager_v1_get_user_data(hwd_workspace_manager_v1);

    struct haywardbar_workspace *workspace = calloc(1, sizeof(struct haywardbar_workspace));
    workspace->workspace_handle = hwd_workspace_handle_v1;
    hwd_workspace_handle_v1_add_listener(
        workspace->workspace_handle, &hwd_workspace_handle_listener, workspace
    );
    hwd_workspace_handle_v1_set_user_data(workspace->workspace_handle, workspace);

    wl_list_insert(&bar->workspaces, &workspace->link);
    workspace->bar = bar;
}

static void
hwd_workspace_manager_handle_done(void *data, struct hwd_workspace_manager_v1 *manager) {
    struct haywardbar *bar = data;

    // TODO atomic state updates.
    determine_bar_visibility(bar, false);
}

static void
hwd_workspace_manager_handle_finished(
    void *data, struct hwd_workspace_manager_v1 *hwd_workspace_manager_v1
) {
    // TODO clean up
}

static const struct hwd_workspace_manager_v1_listener hwd_workspace_manager_listener = {
    .workspace = hwd_workspace_manager_handle_workspace,
    .done = hwd_workspace_manager_handle_done,
    .finished = hwd_workspace_manager_handle_finished,
};

static void
add_xdg_output(struct haywardbar_output *output) {
    if (output->xdg_output != NULL) {
        return;
    }
    assert(output->bar->xdg_output_manager != NULL);
    output->xdg_output =
        zxdg_output_manager_v1_get_xdg_output(output->bar->xdg_output_manager, output->output);
    zxdg_output_v1_add_listener(output->xdg_output, &xdg_output_listener, output);
}

static void
handle_global(
    void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version
) {
    struct haywardbar *bar = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        bar->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        struct haywardbar_seat *seat = calloc(1, sizeof(struct haywardbar_seat));
        if (!seat) {
            hwd_abort("Failed to allocate haywardbar_seat");
            return;
        }
        seat->bar = bar;
        seat->wl_name = name;
        seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
        wl_list_insert(&bar->seats, &seat->link);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        bar->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        struct haywardbar_output *output = calloc(1, sizeof(struct haywardbar_output));
        output->bar = bar;
        output->output = wl_registry_bind(registry, name, &wl_output_interface, 3);
        wl_output_add_listener(output->output, &output_listener, output);
        output->scale = 1;
        output->wl_name = name;
        wl_list_init(&output->hotspots);
        wl_list_init(&output->link);
        if (bar->xdg_output_manager != NULL) {
            add_xdg_output(output);
        }
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        bar->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        bar->xdg_output_manager =
            wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 2);
    } else if (strcmp(interface, hwd_workspace_manager_v1_interface.name) == 0) {
        bar->hwd_workspace_manager =
            wl_registry_bind(registry, name, &hwd_workspace_manager_v1_interface, 1);
        hwd_workspace_manager_v1_add_listener(
            bar->hwd_workspace_manager, &hwd_workspace_manager_listener, bar
        );
        hwd_workspace_manager_v1_set_user_data(bar->hwd_workspace_manager, bar);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct haywardbar *bar = data;
    struct haywardbar_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &bar->outputs, link) {
        if (output->wl_name == name) {
            haywardbar_output_free(output);
            return;
        }
    }
    wl_list_for_each_safe(output, tmp, &bar->unused_outputs, link) {
        if (output->wl_name == name) {
            haywardbar_output_free(output);
            return;
        }
    }
    struct haywardbar_workspace *workspace, *tmp_workspace;
    wl_list_for_each_safe(workspace, tmp_workspace, &bar->workspaces, link) {
        if (workspace->wl_name == name) {
            haywardbar_workspace_free(workspace);
            return;
        }
    }
    struct haywardbar_seat *seat, *tmp_seat;
    wl_list_for_each_safe(seat, tmp_seat, &bar->seats, link) {
        if (seat->wl_name == name) {
            haywardbar_seat_free(seat);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

bool
bar_setup(struct haywardbar *bar, const char *socket_path) {
    bar->visible = true;
    bar->config = init_config();
    wl_list_init(&bar->outputs);
    wl_list_init(&bar->unused_outputs);
    wl_list_init(&bar->workspaces);
    wl_list_init(&bar->seats);
    bar->eventloop = loop_create();

    bar->ipc_socketfd = ipc_open_socket(socket_path);
    bar->ipc_event_socketfd = ipc_open_socket(socket_path);
    if (!ipc_initialize(bar)) {
        return false;
    }

    bar->display = wl_display_connect(NULL);
    if (!bar->display) {
        hwd_abort("Unable to connect to the compositor. "
                  "If your compositor is running, check or set the "
                  "WAYLAND_DISPLAY environment variable.");
    }

    struct wl_registry *registry = wl_display_get_registry(bar->display);
    wl_registry_add_listener(registry, &registry_listener, bar);
    wl_display_roundtrip(bar->display);
    assert(bar->compositor && bar->layer_shell && bar->shm && bar->xdg_output_manager);

    // Second roundtrip for xdg-output
    wl_display_roundtrip(bar->display);

    struct haywardbar_seat *seat;
    wl_list_for_each(seat, &bar->seats, link) {
        struct haywardbar_pointer *pointer = &seat->pointer;
        if (!pointer) {
            continue;
        }
        pointer->cursor_surface = wl_compositor_create_surface(bar->compositor);
        assert(pointer->cursor_surface);
    }

    if (bar->config->status_command) {
        bar->status = status_line_init(bar->config->status_command);
        bar->status->bar = bar;
    }

#if HAVE_TRAY
    if (!bar->config->tray_hidden) {
        bar->tray = create_tray(bar);
    }
#endif

    determine_bar_visibility(bar, false);
    return true;
}

static void
display_in(int fd, short mask, void *data) {
    struct haywardbar *bar = data;
    if (mask & (POLLHUP | POLLERR)) {
        if (mask & POLLERR) {
            hwd_log(HWD_ERROR, "Wayland display poll error");
        }
        bar->running = false;
        return;
    }
    if (wl_display_dispatch(bar->display) == -1) {
        hwd_log(HWD_ERROR, "wl_display_dispatch failed");
        bar->running = false;
    }
}

static void
ipc_in(int fd, short mask, void *data) {
    struct haywardbar *bar = data;
    if (mask & (POLLHUP | POLLERR)) {
        if (mask & POLLERR) {
            hwd_log(HWD_ERROR, "IPC poll error");
        }
        bar->running = false;
        return;
    }
    if (handle_ipc_readable(bar)) {
        set_bar_dirty(bar);
    }
}

void
status_in(int fd, short mask, void *data) {
    struct haywardbar *bar = data;
    if (mask & (POLLHUP | POLLERR)) {
        status_error(bar->status, "[error reading from status command]");
        set_bar_dirty(bar);
        loop_remove_fd(bar->eventloop, fd);
    } else if (status_handle_readable(bar->status)) {
        set_bar_dirty(bar);
    }
}

void
bar_run(struct haywardbar *bar) {
    loop_add_fd(bar->eventloop, wl_display_get_fd(bar->display), POLLIN, display_in, bar);
    loop_add_fd(bar->eventloop, bar->ipc_event_socketfd, POLLIN, ipc_in, bar);
    if (bar->status) {
        loop_add_fd(bar->eventloop, bar->status->read_fd, POLLIN, status_in, bar);
    }
#if HAVE_TRAY
    if (bar->tray) {
        loop_add_fd(bar->eventloop, bar->tray->fd, POLLIN, tray_in, bar->tray->bus);
    }
#endif
    while (bar->running) {
        errno = 0;
        if (wl_display_flush(bar->display) == -1 && errno != EAGAIN) {
            break;
        }
        loop_poll(bar->eventloop);
    }
}

static void
free_outputs(struct wl_list *list) {
    struct haywardbar_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, list, link) { haywardbar_output_free(output); }
}

static void
free_seats(struct wl_list *list) {
    struct haywardbar_seat *seat, *tmp;
    wl_list_for_each_safe(seat, tmp, list, link) { haywardbar_seat_free(seat); }
}

void
bar_teardown(struct haywardbar *bar) {
#if HAVE_TRAY
    destroy_tray(bar->tray);
#endif
    free_outputs(&bar->outputs);
    free_outputs(&bar->unused_outputs);
    free_seats(&bar->seats);
    if (bar->config) {
        free_config(bar->config);
    }
    close(bar->ipc_event_socketfd);
    close(bar->ipc_socketfd);
    if (bar->status) {
        status_line_free(bar->status);
    }
    free(bar->id);
    free(bar->mode);
}
