#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/view.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
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

void
view_set_activated(struct hwd_view *view, bool activated) {
    if (view->impl->set_activated) {
        view->impl->set_activated(view, activated);
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

void
view_update_size(struct hwd_view *view) {
    struct hwd_window *window = view->window;
    window->floating_width = view->geometry.width;
    window->floating_height = view->geometry.height;
    window_set_dirty(window);
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
    if (!wl_list_empty(&view->layers.content_tree->children)) {
        wlr_scene_subsurface_tree_set_clip(
            &view->layers.content_tree->node,
            &(struct wlr_box){.x = x, .y = y, .width = width, .height = height}
        );
    }
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

bool
view_is_visible(struct hwd_view *view) {
    if (view->window->dead) {
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
