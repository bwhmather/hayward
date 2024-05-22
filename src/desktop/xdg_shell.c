#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/desktop/xdg_shell.h"

#include <assert.h>
#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include <xdg-shell-protocol.h>

#include <hayward/globals/root.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_move.h>
#include <hayward/input/seatop_resize_floating.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#define HWD_XDG_SHELL_VERSION 5

static struct hwd_xdg_popup *
hwd_xdg_popup_create(
    struct wlr_xdg_popup *wlr_popup, struct hwd_view *view, struct wlr_scene_tree *parent
);

static void
hwd_xdg_popup_handle_xdg_surface_new_popup(struct wl_listener *listener, void *data) {
    struct hwd_xdg_popup *self = wl_container_of(listener, self, xdg_surface_new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    hwd_xdg_popup_create(wlr_popup, self->view, self->xdg_surface_tree);
}

static void
hwd_xdg_popup_handle_wlr_surface_commit(struct wl_listener *listener, void *data) {
    struct hwd_xdg_popup *self = wl_container_of(listener, self, wlr_surface_commit);

    if (self->wlr_xdg_popup->base->initial_commit) {
        wlr_xdg_surface_schedule_configure(self->wlr_xdg_popup->base);
    }
}

static void
hwd_xdg_popup_handle_xdg_surface_destroy(struct wl_listener *listener, void *data) {
    struct hwd_xdg_popup *self = wl_container_of(listener, self, xdg_surface_destroy);

    wl_list_remove(&self->xdg_surface_new_popup.link);
    wl_list_remove(&self->xdg_surface_destroy.link);
    wl_list_remove(&self->wlr_surface_commit.link);
    wlr_scene_node_destroy(&self->scene_tree->node);
    free(self);
}

static void
popup_unconstrain(struct hwd_xdg_popup *popup) {
    struct hwd_view *view = popup->view;
    struct wlr_xdg_popup *wlr_popup = popup->wlr_xdg_popup;

    struct hwd_window *window = view->window;
    assert(window != NULL);

    struct hwd_output *output = window_get_output(window);
    assert(output != NULL);

    // the output box expressed in the coordinate system of the toplevel parent
    // of the popup
    struct wlr_box output_toplevel_sx_box = {
        .x = output->pending.x - view->window->pending.content_x + view->geometry.x,
        .y = output->pending.y - view->window->pending.content_y + view->geometry.y,
        .width = output->pending.width,
        .height = output->pending.height,
    };

    wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static struct hwd_xdg_popup *
hwd_xdg_popup_create(
    struct wlr_xdg_popup *wlr_popup, struct hwd_view *view, struct wlr_scene_tree *parent
) {
    struct wlr_xdg_surface *xdg_surface = wlr_popup->base;

    struct hwd_xdg_popup *self = calloc(1, sizeof(struct hwd_xdg_popup));
    if (self == NULL) {
        return NULL;
    }

    self->scene_tree = wlr_scene_tree_create(parent);
    assert(self->scene_tree != NULL);

    self->xdg_surface_tree = wlr_scene_xdg_surface_create(self->scene_tree, xdg_surface);
    assert(self->xdg_surface_tree != NULL);

    // TODO scene descriptor.

    self->view = view;

    self->wlr_xdg_popup = xdg_surface->popup;
    struct hwd_xdg_shell_view *shell_view = wl_container_of(view, shell_view, view);
    xdg_surface->data = shell_view;

    wl_signal_add(&xdg_surface->events.new_popup, &self->xdg_surface_new_popup);
    self->xdg_surface_new_popup.notify = hwd_xdg_popup_handle_xdg_surface_new_popup;
    wl_signal_add(&xdg_surface->surface->events.commit, &self->wlr_surface_commit);
    self->wlr_surface_commit.notify = hwd_xdg_popup_handle_wlr_surface_commit;
    wl_signal_add(&xdg_surface->events.destroy, &self->xdg_surface_destroy);
    self->xdg_surface_destroy.notify = hwd_xdg_popup_handle_xdg_surface_destroy;

    popup_unconstrain(self);

    return self;
}

static struct hwd_xdg_shell_view *
xdg_shell_view_from_view(struct hwd_view *view) {
    assert(view->type == HWD_VIEW_XDG_SHELL);
    return (struct hwd_xdg_shell_view *)view;
}

static void
get_constraints(
    struct hwd_view *view, double *min_width, double *max_width, double *min_height,
    double *max_height
) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(view);

    struct wlr_xdg_toplevel_state *state = &self->wlr_xdg_toplevel->current;

    *min_width = state->min_width > 0 ? state->min_width : DBL_MIN;
    *max_width = state->max_width > 0 ? state->max_width : DBL_MAX;
    *min_height = state->min_height > 0 ? state->min_height : DBL_MIN;
    *max_height = state->max_height > 0 ? state->max_height : DBL_MAX;
}

static void
hwd_xdg_shell_view_handle_window_commit(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, window_commit);
    struct hwd_view *view = &self->view;
    struct hwd_window *window = view->window;

    if (!view_is_visible(view)) {
        return;
    }

    bool dirty = false;

    if (self->force_reconfigure) {
        self->force_reconfigure = false;
        dirty = true;
    }

    bool tiled = window_is_tiling(window);
    if (tiled != self->configured_is_tiled) {
        self->configured_is_tiled = tiled;

        enum wlr_edges edges = WLR_EDGE_NONE;
        if (window_is_tiling(window)) {
            edges = WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM;
        }
        wlr_xdg_toplevel_set_tiled(self->wlr_xdg_toplevel, edges);

        dirty = true;
    }

    bool fullscreen = window_is_fullscreen(window);
    if (fullscreen != self->configured_is_fullscreen) {
        self->configured_is_fullscreen = fullscreen;

        wlr_xdg_toplevel_set_fullscreen(self->wlr_xdg_toplevel, fullscreen);

        dirty = true;
    }

    double width = window->pending.width;
    double height = window->pending.height;
    if (width != self->configured_width || height != self->configured_height) {
        self->configured_width = width;
        self->configured_height = height;

        wlr_xdg_toplevel_set_size(self->wlr_xdg_toplevel, width, height);

        dirty = true;
    }

    if (dirty) {
        self->configure_serial = wlr_xdg_surface_schedule_configure(self->wlr_xdg_toplevel->base);
        window_begin_configure(window);
    }
}

static void
set_activated(struct hwd_view *view, bool activated) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(view);

    wlr_xdg_toplevel_set_activated(self->wlr_xdg_toplevel, activated);
}

static void
set_resizing(struct hwd_view *view, bool resizing) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(view);

    wlr_xdg_toplevel_set_resizing(self->wlr_xdg_toplevel, resizing);
}

static bool
wants_floating(struct hwd_xdg_shell_view *self) {
    struct wlr_xdg_toplevel *toplevel = self->wlr_xdg_toplevel;
    struct wlr_xdg_toplevel_state *state = &toplevel->current;

    if (state->max_width != 0) {
        return true;
    }
    if (state->max_height != 0) {
        return true;
    }
    if (toplevel->parent != NULL) {
        return true;
    }
    return false;
}

static bool
is_transient_for(struct hwd_view *child, struct hwd_view *ancestor) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(child);

    struct wlr_xdg_toplevel *toplevel = self->wlr_xdg_toplevel;
    while (toplevel) {
        if (toplevel->parent == xdg_shell_view_from_view(ancestor)->wlr_xdg_toplevel) {
            return true;
        }
        toplevel = toplevel->parent;
    }
    return false;
}

static void
_close(struct hwd_view *view) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(view);

    wlr_xdg_toplevel_send_close(self->wlr_xdg_toplevel);
}

static void
close_popups(struct hwd_view *view) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(view);

    struct wlr_xdg_popup *popup, *tmp;
    wl_list_for_each_safe(popup, tmp, &self->wlr_xdg_toplevel->base->popups, link) {
        wlr_xdg_popup_destroy(popup);
    }
}

static void
destroy(struct hwd_view *view) {
    struct hwd_xdg_shell_view *self = xdg_shell_view_from_view(view);
    free(self);
}

static const struct hwd_view_impl view_impl = {
    .get_constraints = get_constraints,
    .set_activated = set_activated,
    .set_resizing = set_resizing,
    .is_transient_for = is_transient_for,
    .close = _close,
    .close_popups = close_popups,
    .destroy = destroy,
};

static bool
view_notify_ready_by_serial(struct hwd_view *view, uint32_t serial) {
    struct hwd_window *window = view->window;
    struct hwd_xdg_shell_view *shell_view = (struct hwd_xdg_shell_view *)view;

    if (!window->is_configuring) {
        return false;
    }
    if (shell_view->configure_serial == 0) {
        return false;
    }
    if (serial != shell_view->configure_serial) {
        return false;
    }

    window_end_configure(window);

    return true;
}

static void
xdg_shell_view_handle_wlr_surface_commit(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, wlr_surface_commit);

    struct hwd_view *view = &self->view;
    struct hwd_window *window = view->window;
    struct wlr_xdg_toplevel *toplevel = self->wlr_xdg_toplevel;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    window_set_minimum_size(window, toplevel->current.min_width, toplevel->current.min_height);
    window_set_maximum_size(window, toplevel->current.max_width, toplevel->current.max_height);

    if (xdg_surface->initial_commit) {
        wlr_xdg_surface_schedule_configure(xdg_surface);
        return;
    }

    if (!xdg_surface->surface->mapped) {
        return;
    }

    struct wlr_box new_geo;
    wlr_xdg_surface_get_geometry(xdg_surface, &new_geo);
    bool new_size = new_geo.width != view->geometry.width ||
        new_geo.height != view->geometry.height || new_geo.x != view->geometry.x ||
        new_geo.y != view->geometry.y;

    if (new_size) {
        // The client changed its surface size in this commit. For floating
        // windows, we resize the window to match. For tiling windows,
        // we only recenter the surface.
        memcpy(&view->geometry, &new_geo, sizeof(struct wlr_box));
        if (window_is_floating(view->window)) {
            // TODO shouldn't need to be sent a configure in the transaction.
            view_update_size(view);
        } else {
            view_center_surface(view);
        }
    }

    bool success = view_notify_ready_by_serial(view, xdg_surface->current.configure_serial);

    // TODO don't send if transaction is in progress.
    if (!success) {
        view_send_frame_done(view);
    }
}

static void
hwd_xdg_shell_view_handle_wlr_toplevel_set_title(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, wlr_toplevel_set_title);

    struct hwd_view *view = &self->view;

    struct hwd_window *window = view->window;
    if (window == NULL) {
        return;
    }

    window_set_title(window, self->wlr_xdg_toplevel->title);
}

static void
hwd_xdg_shell_view_handle_wlr_toplevel_set_app_id(struct wl_listener *listener, void *data) {}

static void
hwd_xdg_shell_view_handle_xdg_surface_new_popup(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, xdg_surface_new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    struct hwd_xdg_popup *popup = hwd_xdg_popup_create(wlr_popup, &self->view, root->layers.popups);
    int lx, ly;
    wlr_scene_node_coords(&popup->view->layers.content_tree->node, &lx, &ly);
    wlr_scene_node_set_position(&popup->scene_tree->node, lx, ly);
}

static void
hwd_xdg_shell_view_handle_wlr_toplevel_request_fullscreen(
    struct wl_listener *listener, void *data
) {
    struct hwd_xdg_shell_view *self =
        wl_container_of(listener, self, wlr_toplevel_request_fullscreen);

    struct wlr_xdg_toplevel *toplevel = self->wlr_xdg_toplevel;
    struct hwd_view *view = &self->view;

    if (!toplevel->base->surface->mapped) {
        return;
    }

    struct hwd_window *window = view->window;

    struct wlr_xdg_toplevel_requested *req = &toplevel->requested;

    struct hwd_output *output = window_get_output(window);

    if (req->fullscreen && req->fullscreen_output && req->fullscreen_output->data) {
        output = req->fullscreen_output->data;
    }

    if (req->fullscreen) {
        window_fullscreen_on_output(window, output);
    } else if (window_is_fullscreen(window)) {
        window_unfullscreen(window);
    }

    // Protocol demands that we force a reconfigure, even if nothing has changed..
    self->force_reconfigure = true;
    window_set_dirty(window);
}

static void
hwd_xdg_shell_view_handle_wlr_toplevel_request_move(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, wlr_toplevel_request_move);

    struct hwd_view *view = &self->view;

    if (window_is_fullscreen(view->window)) {
        return;
    }

    struct wlr_xdg_toplevel_move_event *e = data;
    struct hwd_seat *seat = e->seat->seat->data;

    if (e->serial == seat->last_button_serial) {
        seatop_begin_move(seat, view->window);
    }
}

static void
hwd_xdg_shell_view_handle_wlr_toplevel_request_resize(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, wlr_toplevel_request_resize);

    struct hwd_view *view = &self->view;

    if (!window_is_floating(view->window)) {
        return;
    }
    struct wlr_xdg_toplevel_resize_event *e = data;
    struct hwd_seat *seat = e->seat->seat->data;

    if (e->serial == seat->last_button_serial) {
        seatop_begin_resize_floating(seat, view->window, e->edges);
    }
}

static void
hwd_xdg_shell_view_handle_xdg_surface_unmap(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, xdg_surface_unmap);

    struct hwd_view *view = &self->view;

    assert(view->surface);

    wl_signal_emit(&view->events.unmap, view);

    struct hwd_column *column = view->window->column;
    struct hwd_workspace *workspace = view->window->workspace;
    window_begin_destroy(view->window);
    if (column) {
        column_consider_destroy(column);
    }
    if (workspace) {
        workspace_consider_destroy(workspace);
    }

    if (workspace && !workspace->dead) {
        workspace_set_dirty(workspace);
        workspace_detect_urgent(workspace);
    }

    root_commit_focus(root);

    view->surface = NULL;

    wl_list_remove(&self->window_commit.link);
    wl_list_remove(&self->wlr_surface_commit.link);
    wl_list_remove(&self->xdg_surface_new_popup.link);
    wl_list_remove(&self->wlr_toplevel_request_fullscreen.link);
    wl_list_remove(&self->wlr_toplevel_request_move.link);
    wl_list_remove(&self->wlr_toplevel_request_resize.link);
    wl_list_remove(&self->wlr_toplevel_set_title.link);
    wl_list_remove(&self->wlr_toplevel_set_app_id.link);
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
hwd_xdg_shell_view_handle_xdg_surface_map(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, xdg_surface_map);

    struct hwd_view *view = &self->view;
    struct wlr_xdg_toplevel *toplevel = self->wlr_xdg_toplevel;
    struct wlr_surface *wlr_surface = toplevel->base->surface;

    assert(view->surface == NULL);
    view->surface = wlr_surface;
    view->window = window_create(root, view);

    // If there is a request to be opened fullscreen on a specific output, try
    // to honor that request. Otherwise, fallback to assigns, pid mappings,
    // focused workspace, etc
    struct hwd_workspace *workspace = root_get_active_workspace(root);
    assert(workspace != NULL);

    struct hwd_output *output = root_get_active_output(root);
    if (toplevel->requested.fullscreen_output && toplevel->requested.fullscreen_output->data) {
        output = toplevel->requested.fullscreen_output->data;
    }
    assert(output != NULL);

    self->window_commit.notify = hwd_xdg_shell_view_handle_window_commit;
    wl_signal_add(&view->window->events.commit, &self->window_commit);

    double natural_width = toplevel->base->current.geometry.width;
    double natural_height = toplevel->base->current.geometry.height;
    if (!natural_width && !natural_height) {
        natural_width = toplevel->base->surface->current.width;
        natural_height = toplevel->base->surface->current.height;
    }
    window_set_natural_size(view->window, natural_width, natural_height);

    if (wants_floating(self)) {
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
            column_set_dirty(view->window->column);
        } else {
            workspace_set_dirty(workspace);
        }
    }

    if (toplevel->requested.fullscreen) {
        // Fullscreen windows still have to have a place as regular
        // tiling or floating windows, so this does not make the
        // previous logic unnecessary.
        window_fullscreen_on_output(view->window, output);
    }

    if (should_focus(view)) {
        root_set_focused_window(root, view->window);
    }

    root_commit_focus(root);

    self->wlr_surface_commit.notify = xdg_shell_view_handle_wlr_surface_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &self->wlr_surface_commit);

    self->xdg_surface_new_popup.notify = hwd_xdg_shell_view_handle_xdg_surface_new_popup;
    wl_signal_add(&toplevel->base->events.new_popup, &self->xdg_surface_new_popup);

    self->wlr_toplevel_request_fullscreen.notify =
        hwd_xdg_shell_view_handle_wlr_toplevel_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &self->wlr_toplevel_request_fullscreen);

    self->wlr_toplevel_request_move.notify = hwd_xdg_shell_view_handle_wlr_toplevel_request_move;
    wl_signal_add(&toplevel->events.request_move, &self->wlr_toplevel_request_move);

    self->wlr_toplevel_request_resize.notify =
        hwd_xdg_shell_view_handle_wlr_toplevel_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &self->wlr_toplevel_request_resize);

    self->wlr_toplevel_set_title.notify = hwd_xdg_shell_view_handle_wlr_toplevel_set_title;
    wl_signal_add(&toplevel->events.set_title, &self->wlr_toplevel_set_title);

    self->wlr_toplevel_set_app_id.notify = hwd_xdg_shell_view_handle_wlr_toplevel_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &self->wlr_toplevel_set_app_id);
}

static void
hwd_xdg_shell_view_handle_xdg_surface_destroy(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *self = wl_container_of(listener, self, xdg_surface_destroy);

    struct hwd_view *view = &self->view;
    assert(view->surface == NULL);

    wl_list_remove(&self->xdg_surface_destroy.link);
    wl_list_remove(&self->xdg_surface_map.link);
    wl_list_remove(&self->xdg_surface_unmap.link);
    self->wlr_xdg_toplevel = NULL;
    view_begin_destroy(view);
}

struct hwd_xdg_shell_view *
hwd_xdg_shell_view_from_wlr_surface(struct wlr_surface *wlr_surface) {
    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(wlr_surface);
    if (xdg_surface != NULL) {
        return xdg_surface->data;
    }

    struct wlr_subsurface *subsurface = wlr_subsurface_try_from_wlr_surface(wlr_surface);
    if (subsurface != NULL) {
        return hwd_xdg_shell_view_from_wlr_surface(subsurface->parent);
    }

    return NULL;
}

static void
hwd_xdg_shell_handle_new_toplevel(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell *self = wl_container_of(listener, self, new_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    struct wlr_xdg_surface *xdg_surface = xdg_toplevel->base;

    wlr_log(
        WLR_DEBUG, "New xdg_shell toplevel title='%s' app_id='%s'", xdg_toplevel->title,
        xdg_toplevel->app_id
    );

    wlr_xdg_surface_ping(xdg_surface);

    struct hwd_xdg_shell_view *xdg_shell_view = calloc(1, sizeof(struct hwd_xdg_shell_view));
    assert(xdg_shell_view);

    xdg_shell_view->xdg_shell = self;

    view_init(&xdg_shell_view->view, HWD_VIEW_XDG_SHELL, &view_impl);
    xdg_shell_view->wlr_xdg_toplevel = xdg_toplevel;

    xdg_shell_view->xdg_surface_map.notify = hwd_xdg_shell_view_handle_xdg_surface_map;
    wl_signal_add(&xdg_surface->surface->events.map, &xdg_shell_view->xdg_surface_map);

    xdg_shell_view->xdg_surface_unmap.notify = hwd_xdg_shell_view_handle_xdg_surface_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &xdg_shell_view->xdg_surface_unmap);

    xdg_shell_view->xdg_surface_destroy.notify = hwd_xdg_shell_view_handle_xdg_surface_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &xdg_shell_view->xdg_surface_destroy);

    wlr_xdg_toplevel_set_wm_capabilities(xdg_toplevel, XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);

    wlr_scene_xdg_surface_create(xdg_shell_view->view.layers.content_tree, xdg_surface);

    xdg_surface->data = xdg_shell_view;
}

struct hwd_xdg_shell *
hwd_xdg_shell_create(struct wl_display *wl_display) {
    struct hwd_xdg_shell *xdg_shell = calloc(1, sizeof(struct hwd_xdg_shell));
    if (xdg_shell == NULL) {
        return NULL;
    }

    xdg_shell->xdg_shell = wlr_xdg_shell_create(wl_display, HWD_XDG_SHELL_VERSION);
    if (xdg_shell->xdg_shell == NULL) {
        free(xdg_shell);
        return NULL;
    }

    xdg_shell->new_toplevel.notify = hwd_xdg_shell_handle_new_toplevel;
    wl_signal_add(&xdg_shell->xdg_shell->events.new_toplevel, &xdg_shell->new_toplevel);

    return xdg_shell;
}
