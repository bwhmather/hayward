#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/xdg_shell.h"

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
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/util/edges.h>

#include <hayward-common/log.h>

#include <hayward/decoration.h>
#include <hayward/globals/root.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_move_floating.h>
#include <hayward/input/seatop_resize_floating.h>
#include <hayward/output.h>
#include <hayward/transaction.h>
#include <hayward/tree.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>
#include <hayward/desktop/xdg_decoration.h>

#include <config.h>

#define HAYWARD_XDG_SHELL_VERSION 2

static struct hayward_xdg_popup *
popup_create(
    struct wlr_xdg_popup *wlr_popup, struct hayward_view *view,
    struct wlr_scene_tree *parent
);

static void
popup_handle_new_popup(struct wl_listener *listener, void *data) {
    struct hayward_xdg_popup *popup =
        wl_container_of(listener, popup, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;
    popup_create(wlr_popup, popup->view, popup->xdg_surface_tree);
}

static void
popup_handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_xdg_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->new_popup.link);
    wl_list_remove(&popup->destroy.link);
    wlr_scene_node_destroy(&popup->scene_tree->node);
    free(popup);
}

static void
popup_unconstrain(struct hayward_xdg_popup *popup) {
    struct hayward_view *view = popup->view;
    struct wlr_xdg_popup *wlr_popup = popup->wlr_xdg_popup;

    struct hayward_window *window = view->window;
    hayward_assert(window != NULL, "Expected window");

    struct hayward_output *output = window_get_output(window);
    hayward_assert(output != NULL, "Expected output");

    // the output box expressed in the coordinate system of the toplevel parent
    // of the popup
    struct wlr_box output_toplevel_sx_box = {
        .x = output->lx - view->window->pending.content_x + view->geometry.x,
        .y = output->ly - view->window->pending.content_y + view->geometry.y,
        .width = output->width,
        .height = output->height,
    };

    wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static struct hayward_xdg_popup *
popup_create(
    struct wlr_xdg_popup *wlr_popup, struct hayward_view *view,
    struct wlr_scene_tree *parent
) {
    struct wlr_xdg_surface *xdg_surface = wlr_popup->base;

    struct hayward_xdg_popup *popup =
        calloc(1, sizeof(struct hayward_xdg_popup));
    if (popup == NULL) {
        return NULL;
    }

    popup->scene_tree = wlr_scene_tree_create(parent);
    hayward_assert(popup->scene_tree != NULL, "Allocation failed");

    popup->xdg_surface_tree =
        wlr_scene_xdg_surface_create(popup->scene_tree, xdg_surface);
    hayward_assert(popup->xdg_surface_tree != NULL, "Allocation failed");

    // TODO scene descriptor.

    popup->view = view;

    popup->wlr_xdg_popup = xdg_surface->popup;
    struct hayward_xdg_shell_view *shell_view =
        wl_container_of(view, shell_view, view);
    xdg_surface->data = shell_view;

    wl_signal_add(&xdg_surface->events.new_popup, &popup->new_popup);
    popup->new_popup.notify = popup_handle_new_popup;
    wl_signal_add(&xdg_surface->events.destroy, &popup->destroy);
    popup->destroy.notify = popup_handle_destroy;

    popup_unconstrain(popup);

    return popup;
}

static struct hayward_xdg_shell_view *
xdg_shell_view_from_view(struct hayward_view *view) {
    hayward_assert(
        view->type == HAYWARD_VIEW_XDG_SHELL, "Expected xdg_shell view"
    );
    return (struct hayward_xdg_shell_view *)view;
}

static void
get_constraints(
    struct hayward_view *view, double *min_width, double *max_width,
    double *min_height, double *max_height
) {
    struct wlr_xdg_toplevel_state *state = &view->wlr_xdg_toplevel->current;
    *min_width = state->min_width > 0 ? state->min_width : DBL_MIN;
    *max_width = state->max_width > 0 ? state->max_width : DBL_MAX;
    *min_height = state->min_height > 0 ? state->min_height : DBL_MIN;
    *max_height = state->max_height > 0 ? state->max_height : DBL_MAX;
}

static const char *
get_string_prop(struct hayward_view *view, enum hayward_view_prop prop) {
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

static uint32_t
configure(
    struct hayward_view *view, double lx, double ly, int width, int height
) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        xdg_shell_view_from_view(view);
    if (xdg_shell_view == NULL) {
        return 0;
    }
    return wlr_xdg_toplevel_set_size(view->wlr_xdg_toplevel, width, height);
}

static void
set_activated(struct hayward_view *view, bool activated) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_set_activated(view->wlr_xdg_toplevel, activated);
}

static void
set_tiled(struct hayward_view *view, bool tiled) {
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
set_fullscreen(struct hayward_view *view, bool fullscreen) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_set_fullscreen(view->wlr_xdg_toplevel, fullscreen);
}

static void
set_resizing(struct hayward_view *view, bool resizing) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_set_resizing(view->wlr_xdg_toplevel, resizing);
}

static bool
wants_floating(struct hayward_view *view) {
    struct wlr_xdg_toplevel *toplevel = view->wlr_xdg_toplevel;
    struct wlr_xdg_toplevel_state *state = &toplevel->current;
    return (state->min_width != 0 && state->min_height != 0 &&
            (state->min_width == state->max_width ||
             state->min_height == state->max_height)) ||
        toplevel->parent;
}

static bool
is_transient_for(struct hayward_view *child, struct hayward_view *ancestor) {
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
_close(struct hayward_view *view) {
    if (xdg_shell_view_from_view(view) == NULL) {
        return;
    }
    wlr_xdg_toplevel_send_close(view->wlr_xdg_toplevel);
}

static void
close_popups(struct hayward_view *view) {
    struct wlr_xdg_popup *popup, *tmp;
    wl_list_for_each_safe(
        popup, tmp, &view->wlr_xdg_toplevel->base->popups, link
    ) {
        wlr_xdg_popup_destroy(popup);
    }
}

static void
destroy(struct hayward_view *view) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        xdg_shell_view_from_view(view);
    if (xdg_shell_view == NULL) {
        return;
    }
    free(xdg_shell_view);
}

static const struct hayward_view_impl view_impl = {
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

static void
view_notify_ready_by_serial(struct hayward_view *view, uint32_t serial) {
    struct hayward_window *window = view->window;

    if (!window->is_configuring) {
        return;
    }
    if (window->configure_serial == 0) {
        return;
    }
    if (serial != window->configure_serial) {
        return;
    }

    transaction_release();
}

static void
handle_commit(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, commit);
    struct hayward_view *view = &xdg_shell_view->view;
    struct wlr_xdg_surface *xdg_surface = view->wlr_xdg_toplevel->base;

    struct wlr_box new_geo;
    wlr_xdg_surface_get_geometry(xdg_surface, &new_geo);
    bool new_size = new_geo.width != view->geometry.width ||
        new_geo.height != view->geometry.height ||
        new_geo.x != view->geometry.x || new_geo.y != view->geometry.y;

    if (new_size) {
        // The client changed its surface size in this commit. For floating
        // containers, we resize the container to match. For tiling containers,
        // we only recenter the surface.
        memcpy(&view->geometry, &new_geo, sizeof(struct wlr_box));
        if (window_is_floating(view->window)) {
            // TODO shouldn't need to be sent a configure in the transaction.
            view_update_size(view);
            transaction_flush();
        } else {
            view_center_surface(view);
        }
    }

    view_notify_ready_by_serial(view, xdg_surface->current.configure_serial);
}

static void
handle_set_title(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, set_title);
    struct hayward_view *view = &xdg_shell_view->view;
    view_update_title(view, false);
}

static void
handle_set_app_id(struct wl_listener *listener, void *data) {}

static void
handle_new_popup(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    struct hayward_xdg_popup *popup =
        popup_create(wlr_popup, &xdg_shell_view->view, root->layers.popups);
    int lx, ly;
    wlr_scene_node_coords(&popup->view->content_tree->node, &lx, &ly);
    wlr_scene_node_set_position(&popup->scene_tree->node, lx, ly);
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, request_fullscreen);
    struct wlr_xdg_toplevel *toplevel = xdg_shell_view->view.wlr_xdg_toplevel;
    struct hayward_view *view = &xdg_shell_view->view;

    if (!toplevel->base->mapped) {
        return;
    }

    struct hayward_window *window = view->window;
    struct wlr_xdg_toplevel_requested *req = &toplevel->requested;
    if (req->fullscreen && req->fullscreen_output &&
        req->fullscreen_output->data) {
        struct hayward_workspace *workspace = root_get_active_workspace(root);
        if (workspace && window->pending.workspace != workspace) {
            hayward_move_window_to_workspace(window, workspace);
        }
    }

    window_set_fullscreen(window, req->fullscreen);

    arrange_root(root);
    transaction_flush();
}

static void
handle_request_move(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, request_move);
    struct hayward_view *view = &xdg_shell_view->view;
    if (!window_is_floating(view->window) || view->window->pending.fullscreen) {
        return;
    }
    struct wlr_xdg_toplevel_move_event *e = data;
    struct hayward_seat *seat = e->seat->seat->data;
    if (e->serial == seat->last_button_serial) {
        seatop_begin_move_floating(seat, view->window);
    }
}

static void
handle_request_resize(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, request_resize);
    struct hayward_view *view = &xdg_shell_view->view;
    if (!window_is_floating(view->window)) {
        return;
    }
    struct wlr_xdg_toplevel_resize_event *e = data;
    struct hayward_seat *seat = e->seat->seat->data;
    if (e->serial == seat->last_button_serial) {
        seatop_begin_resize_floating(seat, view->window, e->edges);
    }
}

static void
handle_unmap(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, unmap);
    struct hayward_view *view = &xdg_shell_view->view;

    hayward_assert(view->surface, "Cannot unmap unmapped view");

    view_unmap(view);

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
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, map);
    struct hayward_view *view = &xdg_shell_view->view;
    struct wlr_xdg_toplevel *toplevel = view->wlr_xdg_toplevel;

    view->natural_width = toplevel->base->current.geometry.width;
    view->natural_height = toplevel->base->current.geometry.height;
    if (!view->natural_width && !view->natural_height) {
        view->natural_width = toplevel->base->surface->current.width;
        view->natural_height = toplevel->base->surface->current.height;
    }

    bool csd = false;

    if (view->xdg_decoration) {
        enum wlr_xdg_toplevel_decoration_v1_mode mode =
            view->xdg_decoration->wlr_xdg_decoration->requested_mode;
        csd = mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    } else {
        struct hayward_server_decoration *deco =
            decoration_from_surface(toplevel->base->surface);
        csd = !deco ||
            deco->wlr_server_decoration->mode ==
                WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
    }

    view_map(
        view, toplevel->base->surface, toplevel->requested.fullscreen,
        toplevel->requested.fullscreen_output, csd
    );

    transaction_flush();

    xdg_shell_view->commit.notify = handle_commit;
    wl_signal_add(
        &toplevel->base->surface->events.commit, &xdg_shell_view->commit
    );

    xdg_shell_view->new_popup.notify = handle_new_popup;
    wl_signal_add(
        &toplevel->base->events.new_popup, &xdg_shell_view->new_popup
    );

    xdg_shell_view->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(
        &toplevel->events.request_fullscreen,
        &xdg_shell_view->request_fullscreen
    );

    xdg_shell_view->request_move.notify = handle_request_move;
    wl_signal_add(
        &toplevel->events.request_move, &xdg_shell_view->request_move
    );

    xdg_shell_view->request_resize.notify = handle_request_resize;
    wl_signal_add(
        &toplevel->events.request_resize, &xdg_shell_view->request_resize
    );

    xdg_shell_view->set_title.notify = handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &xdg_shell_view->set_title);

    xdg_shell_view->set_app_id.notify = handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &xdg_shell_view->set_app_id);
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_xdg_shell_view *xdg_shell_view =
        wl_container_of(listener, xdg_shell_view, destroy);
    struct hayward_view *view = &xdg_shell_view->view;
    hayward_assert(view->surface == NULL, "Tried to destroy a mapped view");
    wl_list_remove(&xdg_shell_view->destroy.link);
    wl_list_remove(&xdg_shell_view->map.link);
    wl_list_remove(&xdg_shell_view->unmap.link);
    view->wlr_xdg_toplevel = NULL;
    if (view->xdg_decoration) {
        view->xdg_decoration->view = NULL;
    }
    view_begin_destroy(view);
}

struct hayward_view *
view_from_wlr_xdg_surface(struct wlr_xdg_surface *xdg_surface) {
    return xdg_surface->data;
}

static void
handle_new_surface(struct wl_listener *listener, void *data) {
    struct wlr_xdg_surface *xdg_surface = data;

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        hayward_log(HAYWARD_DEBUG, "New xdg_shell popup");
        return;
    }

    hayward_log(
        HAYWARD_DEBUG, "New xdg_shell toplevel title='%s' app_id='%s'",
        xdg_surface->toplevel->title, xdg_surface->toplevel->app_id
    );
    wlr_xdg_surface_ping(xdg_surface);

    struct hayward_xdg_shell_view *xdg_shell_view =
        calloc(1, sizeof(struct hayward_xdg_shell_view));
    hayward_assert(xdg_shell_view, "Failed to allocate view");

    view_init(&xdg_shell_view->view, HAYWARD_VIEW_XDG_SHELL, &view_impl);
    xdg_shell_view->view.wlr_xdg_toplevel = xdg_surface->toplevel;

    xdg_shell_view->map.notify = handle_map;
    wl_signal_add(&xdg_surface->events.map, &xdg_shell_view->map);

    xdg_shell_view->unmap.notify = handle_unmap;
    wl_signal_add(&xdg_surface->events.unmap, &xdg_shell_view->unmap);

    xdg_shell_view->destroy.notify = handle_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &xdg_shell_view->destroy);

    xdg_surface->data = xdg_shell_view;
    wlr_scene_xdg_surface_create(
        xdg_shell_view->view.content_tree, xdg_surface
    );
}

struct hayward_xdg_shell *
hayward_xdg_shell_create(struct wl_display *wl_display) {
    struct hayward_xdg_shell *xdg_shell =
        calloc(1, sizeof(struct hayward_xdg_shell));
    if (xdg_shell == NULL) {
        return NULL;
    }

    xdg_shell->xdg_shell =
        wlr_xdg_shell_create(wl_display, HAYWARD_XDG_SHELL_VERSION);
    if (xdg_shell->xdg_shell == NULL) {
        free(xdg_shell);
        return NULL;
    }

    xdg_shell->new_surface.notify = handle_new_surface;
    wl_signal_add(
        &xdg_shell->xdg_shell->events.new_surface, &xdg_shell->new_surface
    );

    return xdg_shell;
}
