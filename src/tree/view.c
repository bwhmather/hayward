#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/view.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/xwayland/xwayland.h>

#include <hayward/config.h>
#include <hayward/desktop/xdg_shell.h>
#include <hayward/desktop/xwayland.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/server.h>
#include <hayward/stringop.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl) {
    view->scene_tree = wlr_scene_tree_create(NULL);
    assert(view->scene_tree != NULL);

    view->layers.content_tree = wlr_scene_tree_create(view->scene_tree);
    assert(view->layers.content_tree != NULL);

    view->type = type;
    view->impl = impl;
    view->allow_request_urgent = true;
    view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DEFAULT;
    wl_signal_init(&view->events.unmap);
}

void
view_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);
    assert(view->destroying);
    assert(view->window == NULL);

    wl_list_remove(&view->events.unmap.listener_list);

    wlr_scene_node_destroy(&view->layers.content_tree->node);
    wlr_scene_node_destroy(&view->scene_tree->node);

    free(view->title_format);

    if (view->impl->destroy) {
        view->impl->destroy(view);
    } else {
        free(view);
    }
}

void
view_begin_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);

    // Unmapping will mark the window as dead and trigger a transaction.  It
    // isn't safe to fully destroy the window until this transaction has
    // completed.  Setting `view->destroying` will tell the window to clean up
    // the view once it has finished cleaning up itself.
    view->destroying = true;
    if (!view->window) {
        view_destroy(view);
    }
}

static const char *
view_get_title(struct hwd_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_TITLE);
    }
    return NULL;
}

static const char *
view_get_app_id(struct hwd_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_APP_ID);
    }
    return NULL;
}

static const char *
view_get_class(struct hwd_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_CLASS);
    }
    return NULL;
}

static const char *
view_get_instance(struct hwd_view *view) {
    if (view->impl->get_string_prop) {
        return view->impl->get_string_prop(view, VIEW_PROP_INSTANCE);
    }
    return NULL;
}

static const char *
view_get_shell(struct hwd_view *view) {
    switch (view->type) {
    case HWD_VIEW_XDG_SHELL:
        return "xdg_shell";
#if HAVE_XWAYLAND
    case HWD_VIEW_XWAYLAND:
        return "xwayland";
#endif
    }
    return "unknown";
}

void
view_get_constraints(
    struct hwd_view *view, double *min_width, double *max_width, double *min_height,
    double *max_height
) {
    if (view->impl->get_constraints) {
        view->impl->get_constraints(view, min_width, max_width, min_height, max_height);
    } else {
        *min_width = DBL_MIN;
        *max_width = DBL_MAX;
        *min_height = DBL_MIN;
        *max_height = DBL_MAX;
    }
}

uint32_t
view_configure(struct hwd_view *view, double lx, double ly, int width, int height) {
    if (view->impl->configure) {
        return view->impl->configure(view, lx, ly, width, height);
    }
    return 0;
}

void
view_set_activated(struct hwd_view *view, bool activated) {
    if (view->impl->set_activated) {
        view->impl->set_activated(view, activated);
    }
    if (view->foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_set_activated(view->foreign_toplevel, activated);
    }
}

void
view_request_activate(struct hwd_view *view) {
    struct hwd_workspace *workspace = view->window->workspace;

    switch (config->focus_on_window_activation) {
    case FOWA_SMART:
        if (workspace_is_visible(workspace)) {
            root_set_focused_window(root, view->window);
        } else {
            view_set_urgent(view, true);
        }
        break;
    case FOWA_URGENT:
        view_set_urgent(view, true);
        break;
    case FOWA_FOCUS:
        root_set_focused_window(root, view->window);
        break;
    case FOWA_NONE:
        break;
    }
}

void
view_set_tiled(struct hwd_view *view, bool tiled) {
    if (view->impl->set_tiled) {
        view->impl->set_tiled(view, tiled);
    }
}

void
view_close(struct hwd_view *view) {
    if (view->impl->close) {
        view->impl->close(view);
    }
}

void
view_close_popups(struct hwd_view *view) {
    if (view->impl->close_popups) {
        view->impl->close_popups(view);
    }
}

static void
view_populate_pid(struct hwd_view *view) {
    pid_t pid;
    switch (view->type) {
#if HAVE_XWAYLAND
    case HWD_VIEW_XWAYLAND:;
        struct wlr_xwayland_surface *surf =
            wlr_xwayland_surface_try_from_wlr_surface(view->surface);
        pid = surf->pid;
        break;
#endif
    case HWD_VIEW_XDG_SHELL:;
        struct wl_client *client = wl_resource_get_client(view->surface->resource);
        wl_client_get_credentials(client, &pid, NULL, NULL);
        break;
    }
    view->pid = pid;
}

static bool
should_focus(struct hwd_view *view) {
    struct hwd_workspace *active_workspace = root_get_active_workspace(root);
    struct hwd_workspace *map_workspace = view->window->workspace;
    struct hwd_output *map_output = window_get_output(view->window);

    // Views cannot be focused if not mapped.
    if (map_workspace == NULL) {
        return false;
    }

    // Views can only take focus if they are mapped into the active workspace.
    if (map_workspace != active_workspace) {
        return false;
    }

    // View opened "under" fullscreen view should not be given focus.
    if (map_output != NULL) {
        struct hwd_window *fullscreen_window =
            workspace_get_fullscreen_window_for_output(map_workspace, map_output);
        if (fullscreen_window != NULL && fullscreen_window != view->window) {
            return false;
        }
    }

    return true;
}

static void
handle_foreign_activate_request(struct wl_listener *listener, void *data) {
    struct hwd_view *view = wl_container_of(listener, view, foreign_activate_request);

    root_set_focused_window(root, view->window);
    window_raise_floating(view->window);
}

static void
handle_foreign_fullscreen_request(struct wl_listener *listener, void *data) {
    struct hwd_view *view = wl_container_of(listener, view, foreign_fullscreen_request);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

    struct hwd_window *window = view->window;

    if (event->fullscreen && event->output && event->output->data) {
        struct hwd_output *output = event->output->data;
        window_fullscreen_on_output(window, output);
    } else {
        window_fullscreen(window);
    }
}

static void
handle_foreign_close_request(struct wl_listener *listener, void *data) {
    struct hwd_view *view = wl_container_of(listener, view, foreign_close_request);

    view_close(view);
}

static void
handle_foreign_destroy(struct wl_listener *listener, void *data) {
    struct hwd_view *view = wl_container_of(listener, view, foreign_destroy);

    wl_list_remove(&view->foreign_activate_request.link);
    wl_list_remove(&view->foreign_fullscreen_request.link);
    wl_list_remove(&view->foreign_close_request.link);
    wl_list_remove(&view->foreign_destroy.link);
}

void
view_map(
    struct hwd_view *view, struct wlr_surface *wlr_surface, bool fullscreen,
    struct wlr_output *fullscreen_output
) {
    assert(view->surface == NULL);
    view->surface = wlr_surface;
    view_populate_pid(view);
    view->window = window_create(root, view);

    // If there is a request to be opened fullscreen on a specific output, try
    // to honor that request. Otherwise, fallback to assigns, pid mappings,
    // focused workspace, etc
    struct hwd_workspace *workspace = root_get_active_workspace(root);
    assert(workspace != NULL);

    struct hwd_output *output = root_get_active_output(root);
    if (fullscreen_output && fullscreen_output->data) {
        output = fullscreen_output->data;
    }
    assert(output != NULL);

    view->foreign_toplevel = wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);
    view->foreign_activate_request.notify = handle_foreign_activate_request;
    wl_signal_add(
        &view->foreign_toplevel->events.request_activate, &view->foreign_activate_request
    );
    view->foreign_fullscreen_request.notify = handle_foreign_fullscreen_request;
    wl_signal_add(
        &view->foreign_toplevel->events.request_fullscreen, &view->foreign_fullscreen_request
    );
    view->foreign_close_request.notify = handle_foreign_close_request;
    wl_signal_add(&view->foreign_toplevel->events.request_close, &view->foreign_close_request);
    view->foreign_destroy.notify = handle_foreign_destroy;
    wl_signal_add(&view->foreign_toplevel->events.destroy, &view->foreign_destroy);

    const char *app_id;
    const char *class;
    if ((app_id = view_get_app_id(view)) != NULL) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id);
    } else if ((class = view_get_class(view)) != NULL) {
        wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, class);
    }

    if (view->impl->wants_floating && view->impl->wants_floating(view)) {
        workspace_add_floating(workspace, view->window);
        window_floating_set_default_size(view->window);
        window_floating_resize_and_center(view->window);

    } else {
        struct hwd_window *target_sibling = workspace_get_active_tiling_window(workspace);
        if (target_sibling) {
            column_add_sibling(target_sibling, view->window, 1);
        } else {
            struct hwd_column *column = column_create();
            workspace_insert_column_first(workspace, output, column);
            column_add_child(column, view->window);
        }

        if (target_sibling) {
            column_set_dirty(view->window->parent);
        } else {
            workspace_set_dirty(workspace);
        }
    }

    if (fullscreen) {
        // Fullscreen windows still have to have a place as regular
        // tiling or floating windows, so this does not make the
        // previous logic unnecessary.
        window_fullscreen_on_output(view->window, output);
    }

    view_update_title(view, false);

    bool set_focus = should_focus(view);

#if HAVE_XWAYLAND
    struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(wlr_surface);
    if (xsurface != NULL) {
        set_focus &= wlr_xwayland_icccm_input_model(xsurface) != WLR_ICCCM_INPUT_MODEL_NONE;
    }
#endif

    if (set_focus) {
        root_set_focused_window(root, view->window);
    }
}

void
view_unmap(struct hwd_view *view) {
    wl_signal_emit(&view->events.unmap, view);

    if (view->urgent_timer) {
        wl_event_source_remove(view->urgent_timer);
        view->urgent_timer = NULL;
    }

    if (view->foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
        view->foreign_toplevel = NULL;
    }

    struct hwd_column *parent = view->window->parent;
    struct hwd_workspace *workspace = view->window->workspace;
    window_begin_destroy(view->window);
    if (parent) {
        column_consider_destroy(parent);
    } else if (workspace) {
        workspace_consider_destroy(workspace);
    }

    if (workspace && !workspace->pending.dead) {
        workspace_set_dirty(workspace);
        workspace_detect_urgent(workspace);
    }

    struct hwd_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        if (seat->cursor->active_constraint) {
            struct wlr_surface *constrain_surface = seat->cursor->active_constraint->surface;
            if (view_from_wlr_surface(constrain_surface) == view) {
                hwd_cursor_constrain(seat->cursor, NULL);
            }
        }
    }

    view->surface = NULL;
}

void
view_update_size(struct hwd_view *view) {
    struct hwd_window *window = view->window;
    window->pending.content_width = view->geometry.width;
    window->pending.content_height = view->geometry.height;
    window_set_geometry_from_content(window);
}

void
view_center_surface(struct hwd_view *view) {
    struct hwd_window *window = view->window;

    // We always center the current coordinates rather than the next, as the
    // geometry immediately affects the currently active rendering.
    int x = (int)fmax(0, (window->committed.content_width - view->geometry.width) / 2);
    int y = (int)fmax(0, (window->committed.content_height - view->geometry.height) / 2);
    int width = (int)window->committed.content_width;
    int height = (int)window->committed.content_height;

    wlr_scene_node_set_position(&view->layers.content_tree->node, x, y);
    wlr_scene_subsurface_tree_set_clip(
        &view->layers.content_tree->node,
        &(struct wlr_box){.x = x, .y = y, .width = width, .height = height}
    );
}

struct hwd_view *
view_from_wlr_surface(struct wlr_surface *wlr_surface) {
    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(wlr_surface);
    if (xdg_surface != NULL) {
        return view_from_wlr_xdg_surface(xdg_surface);
    }
#if HAVE_XWAYLAND
    struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(wlr_surface);
    if (xsurface != NULL) {
        return view_from_wlr_xwayland_surface(xsurface);
    }
#endif
    struct wlr_subsurface *subsurface = wlr_subsurface_try_from_wlr_surface(wlr_surface);
    if (subsurface != NULL) {
        return view_from_wlr_surface(subsurface->parent);
    }
    if (wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface)) {
        return NULL;
    }

    const char *role = wlr_surface->role ? wlr_surface->role->name : NULL;
    wlr_log(WLR_DEBUG, "Surface of unknown type (role %s): %p", role, (void *)wlr_surface);
    return NULL;
}

static size_t
append_prop(char *buffer, const char *value) {
    if (!value) {
        return 0;
    }
    lenient_strcat(buffer, value);
    return strlen(value);
}

/**
 * Calculate and return the length of the formatted title.
 * If buffer is not NULL, also populate the buffer with the formatted title.
 */
static size_t
parse_title_format(struct hwd_view *view, char *buffer) {
    if (!view->title_format || strcmp(view->title_format, "%title") == 0) {
        return append_prop(buffer, view_get_title(view));
    }

    size_t len = 0;
    char *format = view->title_format;
    char *next = strchr(format, '%');
    while (next) {
        // Copy everything up to the %
        lenient_strncat(buffer, format, next - format);
        len += next - format;
        format = next;

        if (strncmp(next, "%title", 6) == 0) {
            len += append_prop(buffer, view_get_title(view));
            format += 6;
        } else if (strncmp(next, "%app_id", 7) == 0) {
            len += append_prop(buffer, view_get_app_id(view));
            format += 7;
        } else if (strncmp(next, "%class", 6) == 0) {
            len += append_prop(buffer, view_get_class(view));
            format += 6;
        } else if (strncmp(next, "%instance", 9) == 0) {
            len += append_prop(buffer, view_get_instance(view));
            format += 9;
        } else if (strncmp(next, "%shell", 6) == 0) {
            len += append_prop(buffer, view_get_shell(view));
            format += 6;
        } else {
            lenient_strcat(buffer, "%");
            ++format;
            ++len;
        }
        next = strchr(format, '%');
    }
    lenient_strcat(buffer, format);
    len += strlen(format);

    return len;
}

void
view_update_title(struct hwd_view *view, bool force) {
    const char *title = view_get_title(view);

    if (!force) {
        if (title && view->window->title && strcmp(title, view->window->title) == 0) {
            return;
        }
        if (!title && !view->window->title) {
            return;
        }
    }

    free(view->window->title);
    free(view->window->formatted_title);
    if (title) {
        size_t len = parse_title_format(view, NULL);
        char *buffer = calloc(len + 1, sizeof(char));
        assert(buffer);
        parse_title_format(view, buffer);

        view->window->title = strdup(title);
        view->window->formatted_title = buffer;
    } else {
        view->window->title = NULL;
        view->window->formatted_title = NULL;
    }

    window_set_dirty(view->window);

    if (view->foreign_toplevel && title) {
        wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title);
    }
}

bool
view_is_visible(struct hwd_view *view) {
    if (view->window->pending.dead) {
        return false;
    }
    struct hwd_workspace *workspace = view->window->workspace;
    if (!workspace) {
        return false;
    }

    struct hwd_output *output = window_get_output(view->window);
    if (!output) {
        return false;
    }

    // Check view isn't in a shaded window.
    struct hwd_window *window = view->window;
    struct hwd_column *column = window->parent;
    if (column != NULL && window->pending.shaded) {
        return false;
    }

    // Check view isn't hidden by another fullscreen view
    struct hwd_window *fs = workspace_get_fullscreen_window_for_output(workspace, output);
    if (fs && !window_is_fullscreen(view->window) && !window_is_transient_for(view->window, fs)) {
        return false;
    }
    return true;
}

void
view_set_urgent(struct hwd_view *view, bool enable) {
    if (view_is_urgent(view) == enable) {
        return;
    }
    if (enable) {
        if (root_get_focused_window(root) == view->window) {
            return;
        }
        clock_gettime(CLOCK_MONOTONIC, &view->urgent);
    } else {
        view->urgent = (struct timespec){0};
        if (view->urgent_timer) {
            wl_event_source_remove(view->urgent_timer);
            view->urgent_timer = NULL;
        }
    }

    workspace_detect_urgent(view->window->workspace);
}

bool
view_is_urgent(struct hwd_view *view) {
    return view->urgent.tv_sec || view->urgent.tv_nsec;
}

static void
view_freeze_buffer_iterator(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct wlr_scene_tree *tree = data;

    struct wlr_scene_buffer *sbuf = wlr_scene_buffer_create(tree, NULL);
    assert(sbuf != NULL);

    wlr_scene_buffer_set_dest_size(sbuf, buffer->dst_width, buffer->dst_height);
    wlr_scene_buffer_set_opaque_region(sbuf, &buffer->opaque_region);
    wlr_scene_buffer_set_source_box(sbuf, &buffer->src_box);
    wlr_scene_node_set_position(&sbuf->node, sx, sy);
    wlr_scene_buffer_set_transform(sbuf, buffer->transform);
    wlr_scene_buffer_set_buffer(sbuf, buffer->buffer);
}

void
view_freeze_buffer(struct hwd_view *view) {
    assert(view->layers.saved_surface_tree == NULL);

    view->layers.saved_surface_tree = wlr_scene_tree_create(view->scene_tree);
    assert(view->layers.saved_surface_tree != NULL);

    // Enable and disable the saved surface tree like so to atomitaclly update
    // the tree. This will prevent over damaging or other weirdness.
    wlr_scene_node_set_enabled(&view->layers.saved_surface_tree->node, false);

    wlr_scene_node_for_each_buffer(
        &view->layers.content_tree->node, view_freeze_buffer_iterator,
        view->layers.saved_surface_tree
    );

    wlr_scene_node_set_enabled(&view->layers.content_tree->node, false);
    wlr_scene_node_set_enabled(&view->layers.saved_surface_tree->node, true);
}

void
view_unfreeze_buffer(struct hwd_view *view) {
    if (view->layers.saved_surface_tree == NULL) {
        return;
    }

    wlr_scene_node_destroy(&view->layers.saved_surface_tree->node);
    view->layers.saved_surface_tree = NULL;

    wlr_scene_node_set_enabled(&view->layers.content_tree->node, true);
}

bool
view_is_transient_for(struct hwd_view *child, struct hwd_view *ancestor) {
    return child->impl->is_transient_for && child->impl->is_transient_for(child, ancestor);
}

static void
send_frame_done_iterator(struct wlr_scene_buffer *scene_buffer, int x, int y, void *data) {
    struct timespec *when = data;
    wl_signal_emit_mutable(&scene_buffer->events.frame_done, when);
}

void
view_send_frame_done(struct hwd_view *view) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    struct wlr_scene_node *node;
    wl_list_for_each(node, &view->layers.content_tree->children, link) {
        wlr_scene_node_for_each_buffer(node, send_frame_done_iterator, &now);
    }
}
