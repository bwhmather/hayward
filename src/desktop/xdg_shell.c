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
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>

#include <xdg-shell-protocol.h>

#include <hayward/globals/root.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_move.h>
#include <hayward/input/seatop_resize_floating.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#define HWD_XDG_SHELL_VERSION 5

static struct hwd_xdg_popup *
popup_create(struct wlr_xdg_popup *wlr_popup, struct hwd_view *view, struct wlr_scene_tree *parent);

static void
popup_handle_new_popup(struct wl_listener *listener, void *data) {
    struct hwd_xdg_popup *popup = wl_container_of(listener, popup, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    popup_create(wlr_popup, popup->view, popup->xdg_surface_tree);
}

static void
popup_handle_commit(struct wl_listener *listener, void *data) {
    struct hwd_xdg_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->wlr_xdg_popup->base->initial_commit) {
        wlr_xdg_surface_schedule_configure(popup->wlr_xdg_popup->base);
    }
}

static void
popup_handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_xdg_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->new_popup.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->commit.link);
    wlr_scene_node_destroy(&popup->scene_tree->node);
    free(popup);
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
popup_create(
    struct wlr_xdg_popup *wlr_popup, struct hwd_view *view, struct wlr_scene_tree *parent
) {
    struct wlr_xdg_surface *xdg_surface = wlr_popup->base;

    struct hwd_xdg_popup *popup = calloc(1, sizeof(struct hwd_xdg_popup));
    if (popup == NULL) {
        return NULL;
    }

    popup->scene_tree = wlr_scene_tree_create(parent);
    assert(popup->scene_tree != NULL);

    popup->xdg_surface_tree = wlr_scene_xdg_surface_create(popup->scene_tree, xdg_surface);
    assert(popup->xdg_surface_tree != NULL);

    // TODO scene descriptor.

    popup->view = view;

    popup->wlr_xdg_popup = xdg_surface->popup;
    struct hwd_xdg_shell_view *shell_view = wl_container_of(view, shell_view, view);
    xdg_surface->data = shell_view;

    wl_signal_add(&xdg_surface->events.new_popup, &popup->new_popup);
    popup->new_popup.notify = popup_handle_new_popup;
    wl_signal_add(&xdg_surface->surface->events.commit, &popup->commit);
    popup->commit.notify = popup_handle_commit;
    wl_signal_add(&xdg_surface->events.destroy, &popup->destroy);
    popup->destroy.notify = popup_handle_destroy;

    popup_unconstrain(popup);

    return popup;
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
    struct wlr_xdg_toplevel_state *state = &view->wlr_xdg_toplevel->current;
    *min_width = state->min_width > 0 ? state->min_width : DBL_MIN;
    *max_width = state->max_width > 0 ? state->max_width : DBL_MAX;
    *min_height = state->min_height > 0 ? state->min_height : DBL_MIN;
    *max_height = state->max_height > 0 ? state->max_height : DBL_MAX;
}

static const char *
get_string_prop(struct hwd_view *view, enum hwd_view_prop prop) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return NULL;
    }
    switch (prop) {
    case VIEW_PROP_TITLE:
        return view->wlr_xdg_toplevel->title;
    case VIEW_PROP_APP_ID:
        return view->wlr_xdg_toplevel->app_id;
    default:
        return NULL;
    }
}

static void
configure(struct hwd_view *view, double lx, double ly, int width, int height) {
    struct hwd_window *window = view->window;
    struct hwd_xdg_shell_view *self = (struct hwd_xdg_shell_view *)view;

    if (!view_is_visible(view)) {
        return;
    }

    if (self->configured_width == width && self->configured_height == height) {
        return;
    }
    self->configured_width = width;
    self->configured_height = height;

    self->configure_serial = wlr_xdg_toplevel_set_size(view->wlr_xdg_toplevel, width, height);

    window_begin_configure(window);
}

static void
set_activated(struct hwd_view *view, bool activated) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_set_activated(view->wlr_xdg_toplevel, activated);
}

static void
set_tiled(struct hwd_view *view, bool tiled) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    enum wlr_edges edges = WLR_EDGE_NONE;
    if (tiled) {
        edges = WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM;
    }
    wlr_xdg_toplevel_set_tiled(view->wlr_xdg_toplevel, edges);
}

static void
set_fullscreen(struct hwd_view *view, bool fullscreen) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_set_fullscreen(view->wlr_xdg_toplevel, fullscreen);
}

static void
set_resizing(struct hwd_view *view, bool resizing) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_set_resizing(view->wlr_xdg_toplevel, resizing);
}

static bool
wants_floating(struct hwd_view *view) {
    struct wlr_xdg_toplevel *toplevel = view->wlr_xdg_toplevel;
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
    if (xdg_shell_view_from_view(child) == NULL) {
        return false;
    }
    struct wlr_xdg_toplevel *toplevel = child->wlr_xdg_toplevel;
    while (toplevel) {
        if (toplevel->parent == ancestor->wlr_xdg_toplevel) {
            return true;
        }
        toplevel = toplevel->parent;
    }
    return false;
}

static void
_close(struct hwd_view *view) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_send_close(view->wlr_xdg_toplevel);
}

static void
close_popups(struct hwd_view *view) {
    struct wlr_xdg_popup *popup, *tmp;
    wl_list_for_each_safe(popup, tmp, &view->wlr_xdg_toplevel->base->popups, link) {
        wlr_xdg_popup_destroy(popup);
    }
}

static void
destroy(struct hwd_view *view) {
    struct hwd_xdg_shell_view *xdg_shell_view = xdg_shell_view_from_view(view);
    if (xdg_shell_view == NULL) {
        return;
    }
    free(xdg_shell_view);
}

static const struct hwd_view_impl view_impl = {
    .get_constraints = get_constraints,
    .get_string_prop = get_string_prop,
    .configure = configure,
    .set_activated = set_activated,
    .set_tiled = set_tiled,
    .set_fullscreen = set_fullscreen,
    .set_resizing = set_resizing,
    .wants_floating = wants_floating,
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
handle_commit(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view = wl_container_of(listener, xdg_shell_view, commit);

    struct hwd_view *view = &xdg_shell_view->view;
    struct wlr_xdg_surface *xdg_surface = view->wlr_xdg_toplevel->base;

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
handle_set_title(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, set_title);

    struct hwd_view *view = &xdg_shell_view->view;

    view_update_title(view, false);
}

static void
handle_set_app_id(struct wl_listener *listener, void *data) {}

static void
handle_new_popup(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    struct hwd_xdg_popup *popup =
        popup_create(wlr_popup, &xdg_shell_view->view, root->layers.popups);
    int lx, ly;
    wlr_scene_node_coords(&popup->view->layers.content_tree->node, &lx, &ly);
    wlr_scene_node_set_position(&popup->scene_tree->node, lx, ly);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, request_fullscreen);

    struct wlr_xdg_toplevel *toplevel = xdg_shell_view->view.wlr_xdg_toplevel;
    struct hwd_view *view = &xdg_shell_view->view;

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
}

static void
handle_request_move(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, request_move);

    struct hwd_view *view = &xdg_shell_view->view;

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
handle_request_resize(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, request_resize);

    struct hwd_view *view = &xdg_shell_view->view;

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
handle_unmap(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view = wl_container_of(listener, xdg_shell_view, unmap);

    struct hwd_view *view = &xdg_shell_view->view;

    assert(view->surface);

    view_unmap(view);
    root_commit_focus(root);

    wl_list_remove(&xdg_shell_view->commit.link);
    wl_list_remove(&xdg_shell_view->new_popup.link);
    wl_list_remove(&xdg_shell_view->request_fullscreen.link);
    wl_list_remove(&xdg_shell_view->request_move.link);
    wl_list_remove(&xdg_shell_view->request_resize.link);
    wl_list_remove(&xdg_shell_view->set_title.link);
    wl_list_remove(&xdg_shell_view->set_app_id.link);
}

static void
handle_map(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view = wl_container_of(listener, xdg_shell_view, map);

    struct hwd_view *view = &xdg_shell_view->view;
    struct wlr_xdg_toplevel *toplevel = view->wlr_xdg_toplevel;

    view->natural_width = toplevel->base->current.geometry.width;
    view->natural_height = toplevel->base->current.geometry.height;
    if (!view->natural_width && !view->natural_height) {
        view->natural_width = toplevel->base->surface->current.width;
        view->natural_height = toplevel->base->surface->current.height;
    }

    view_map(
        view, toplevel->base->surface, toplevel->requested.fullscreen,
        toplevel->requested.fullscreen_output
    );
    root_commit_focus(root);

    xdg_shell_view->commit.notify = handle_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &xdg_shell_view->commit);

    xdg_shell_view->new_popup.notify = handle_new_popup;
    wl_signal_add(&toplevel->base->events.new_popup, &xdg_shell_view->new_popup);

    xdg_shell_view->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &xdg_shell_view->request_fullscreen);

    xdg_shell_view->request_move.notify = handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &xdg_shell_view->request_move);

    xdg_shell_view->request_resize.notify = handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &xdg_shell_view->request_resize);

    xdg_shell_view->set_title.notify = handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &xdg_shell_view->set_title);

    xdg_shell_view->set_app_id.notify = handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &xdg_shell_view->set_app_id);
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell_view *xdg_shell_view = wl_container_of(listener, xdg_shell_view, destroy);

    struct hwd_view *view = &xdg_shell_view->view;
    assert(view->surface == NULL);

    wl_list_remove(&xdg_shell_view->destroy.link);
    wl_list_remove(&xdg_shell_view->map.link);
    wl_list_remove(&xdg_shell_view->unmap.link);
    view->wlr_xdg_toplevel = NULL;
    view_begin_destroy(view);
}

struct hwd_view *
view_from_wlr_xdg_surface(struct wlr_xdg_surface *xdg_surface) {
    return xdg_surface->data;
}

static void
handle_new_toplevel(struct wl_listener *listener, void *data) {
    struct hwd_xdg_shell *xdg_shell = wl_container_of(listener, xdg_shell, new_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;
    struct wlr_xdg_surface *xdg_surface = xdg_toplevel->base;

    wlr_log(
        WLR_DEBUG, "New xdg_shell toplevel title='%s' app_id='%s'", xdg_toplevel->title,
        xdg_toplevel->app_id
    );

    wlr_xdg_surface_ping(xdg_surface);

    struct hwd_xdg_shell_view *xdg_shell_view = calloc(1, sizeof(struct hwd_xdg_shell_view));
    assert(xdg_shell_view);

    xdg_shell_view->xdg_shell = xdg_shell;

    view_init(&xdg_shell_view->view, HWD_VIEW_XDG_SHELL, &view_impl);
    xdg_shell_view->view.wlr_xdg_toplevel = xdg_toplevel;

    xdg_shell_view->map.notify = handle_map;
    wl_signal_add(&xdg_surface->surface->events.map, &xdg_shell_view->map);

    xdg_shell_view->unmap.notify = handle_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &xdg_shell_view->unmap);

    xdg_shell_view->destroy.notify = handle_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &xdg_shell_view->destroy);

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

    xdg_shell->new_toplevel.notify = handle_new_toplevel;
    wl_signal_add(&xdg_shell->xdg_shell->events.new_toplevel, &xdg_shell->new_toplevel);

    return xdg_shell;
}
