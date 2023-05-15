#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <float.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/box.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xproto.h>

#include <hayward-common/log.h>

#include <hayward/desktop/transaction.h>
#include <hayward/globals/root.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/server.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/xwayland.h>

#include <config.h>

static const char *atom_map[ATOM_LAST] = {
    [NET_WM_WINDOW_TYPE_NORMAL] = "_NET_WM_WINDOW_TYPE_NORMAL",
    [NET_WM_WINDOW_TYPE_DIALOG] = "_NET_WM_WINDOW_TYPE_DIALOG",
    [NET_WM_WINDOW_TYPE_UTILITY] = "_NET_WM_WINDOW_TYPE_UTILITY",
    [NET_WM_WINDOW_TYPE_TOOLBAR] = "_NET_WM_WINDOW_TYPE_TOOLBAR",
    [NET_WM_WINDOW_TYPE_SPLASH] = "_NET_WM_WINDOW_TYPE_SPLASH",
    [NET_WM_WINDOW_TYPE_MENU] = "_NET_WM_WINDOW_TYPE_MENU",
    [NET_WM_WINDOW_TYPE_DROPDOWN_MENU] = "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
    [NET_WM_WINDOW_TYPE_POPUP_MENU] = "_NET_WM_WINDOW_TYPE_POPUP_MENU",
    [NET_WM_WINDOW_TYPE_TOOLTIP] = "_NET_WM_WINDOW_TYPE_TOOLTIP",
    [NET_WM_WINDOW_TYPE_NOTIFICATION] = "_NET_WM_WINDOW_TYPE_NOTIFICATION",
    [NET_WM_STATE_MODAL] = "_NET_WM_STATE_MODAL",
};

static const struct wlr_addon_interface
    unmanaged_surface_scene_marker_interface = {
        .name = "hayward_xwayland_unmanaged", .destroy = NULL};

static void
unmanaged_handle_request_configure(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_unmanaged *surface =
        wl_container_of(listener, surface, request_configure);
    struct wlr_xwayland_surface *xsurface = surface->wlr_xwayland_surface;
    struct wlr_xwayland_surface_configure_event *ev = data;
    wlr_xwayland_surface_configure(
        xsurface, ev->x, ev->y, ev->width, ev->height
    );
}

static void
unmanaged_handle_set_geometry(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_unmanaged *surface =
        wl_container_of(listener, surface, set_geometry);
    struct wlr_xwayland_surface *xsurface = surface->wlr_xwayland_surface;

    wlr_scene_node_set_position(
        &surface->surface_scene->buffer->node, xsurface->x, xsurface->y
    );
}

static void
unmanaged_handle_map(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_unmanaged *surface =
        wl_container_of(listener, surface, map);
    struct wlr_xwayland_surface *xsurface = surface->wlr_xwayland_surface;

    surface->surface_scene =
        wlr_scene_surface_create(root->layers.unmanaged, xsurface->surface);

    if (surface->surface_scene) {
        wlr_addon_init(
            &surface->surface_scene_marker,
            &surface->surface_scene->buffer->node.addons,
            &unmanaged_surface_scene_marker_interface,
            &unmanaged_surface_scene_marker_interface
        );

        wlr_scene_node_set_position(
            &surface->surface_scene->buffer->node, xsurface->x, xsurface->y
        );

        wl_signal_add(&xsurface->events.set_geometry, &surface->set_geometry);
        surface->set_geometry.notify = unmanaged_handle_set_geometry;
    }

    if (wlr_xwayland_or_surface_wants_focus(xsurface)) {
        struct hayward_seat *seat = input_manager_current_seat();
        struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
        wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
        root_set_focused_surface(root, xsurface->surface);
    }
}

static void
unmanaged_handle_unmap(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_unmanaged *surface =
        wl_container_of(listener, surface, unmap);
    struct wlr_xwayland_surface *xsurface = surface->wlr_xwayland_surface;

    if (surface->surface_scene) {
        wl_list_remove(&surface->set_geometry.link);

        wlr_scene_node_destroy(&surface->surface_scene->buffer->node);
        surface->surface_scene = NULL;
    }

    struct hayward_seat *seat = input_manager_current_seat();
    if (seat->wlr_seat->keyboard_state.focused_surface == xsurface->surface) {
        // This simply returns focus to the parent surface if there's one
        // available. This seems to handle JetBrains issues.
        if (xsurface->parent && xsurface->parent->surface &&
            wlr_xwayland_or_surface_wants_focus(xsurface->parent)) {
            root_set_focused_surface(root, xsurface->parent->surface);
            return;
        }
    }
}

static void
unmanaged_handle_request_activate(struct wl_listener *listener, void *data) {
    struct wlr_xwayland_surface *xsurface = data;
    if (!xsurface->mapped) {
        return;
    }
    struct hayward_window *focus = root_get_focused_window(root);
    if (focus && focus->view && focus->view->pid != xsurface->pid) {
        return;
    }

    root_set_focused_surface(root, xsurface->surface);
}

static void
unmanaged_handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_unmanaged *surface =
        wl_container_of(listener, surface, destroy);
    wl_list_remove(&surface->request_configure.link);
    wl_list_remove(&surface->map.link);
    wl_list_remove(&surface->unmap.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->override_redirect.link);
    wl_list_remove(&surface->request_activate.link);
    free(surface);
}

static void
handle_map(struct wl_listener *listener, void *data);

struct hayward_xwayland_view *
create_xwayland_view(struct wlr_xwayland_surface *xsurface);

static void
unmanaged_handle_override_redirect(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_unmanaged *surface =
        wl_container_of(listener, surface, override_redirect);
    struct wlr_xwayland_surface *xsurface = surface->wlr_xwayland_surface;

    bool mapped = xsurface->mapped;
    if (mapped) {
        unmanaged_handle_unmap(&surface->unmap, NULL);
    }

    unmanaged_handle_destroy(&surface->destroy, NULL);
    xsurface->data = NULL;
    struct hayward_xwayland_view *xwayland_view =
        create_xwayland_view(xsurface);
    if (mapped) {
        handle_map(&xwayland_view->map, xsurface);
    }
}

static struct hayward_xwayland_unmanaged *
create_unmanaged(struct wlr_xwayland_surface *xsurface) {
    struct hayward_xwayland_unmanaged *surface =
        calloc(1, sizeof(struct hayward_xwayland_unmanaged));
    if (surface == NULL) {
        hayward_log(HAYWARD_ERROR, "Allocation failed");
        return NULL;
    }

    surface->wlr_xwayland_surface = xsurface;

    wl_signal_add(
        &xsurface->events.request_configure, &surface->request_configure
    );
    surface->request_configure.notify = unmanaged_handle_request_configure;
    wl_signal_add(&xsurface->events.map, &surface->map);
    surface->map.notify = unmanaged_handle_map;
    wl_signal_add(&xsurface->events.unmap, &surface->unmap);
    surface->unmap.notify = unmanaged_handle_unmap;
    wl_signal_add(&xsurface->events.destroy, &surface->destroy);
    surface->destroy.notify = unmanaged_handle_destroy;
    wl_signal_add(
        &xsurface->events.set_override_redirect, &surface->override_redirect
    );
    surface->override_redirect.notify = unmanaged_handle_override_redirect;
    wl_signal_add(
        &xsurface->events.request_activate, &surface->request_activate
    );
    surface->request_activate.notify = unmanaged_handle_request_activate;

    return surface;
}

static struct hayward_xwayland_view *
xwayland_view_from_view(struct hayward_view *view) {
    hayward_assert(
        view->type == HAYWARD_VIEW_XWAYLAND, "Expected xwayland view"
    );
    return (struct hayward_xwayland_view *)view;
}

static const char *
get_string_prop(struct hayward_view *view, enum hayward_view_prop prop) {
    if (xwayland_view_from_view(view) == NULL) {
        return NULL;
    }
    switch (prop) {
    case VIEW_PROP_TITLE:
        return view->wlr_xwayland_surface->title;
    case VIEW_PROP_CLASS:
        return view->wlr_xwayland_surface->class;
    case VIEW_PROP_INSTANCE:
        return view->wlr_xwayland_surface->instance;
    case VIEW_PROP_WINDOW_ROLE:
        return view->wlr_xwayland_surface->role;
    default:
        return NULL;
    }
}

static uint32_t
get_int_prop(struct hayward_view *view, enum hayward_view_prop prop) {
    if (xwayland_view_from_view(view) == NULL) {
        return 0;
    }
    switch (prop) {
    case VIEW_PROP_X11_WINDOW_ID:
        return view->wlr_xwayland_surface->window_id;
    case VIEW_PROP_X11_PARENT_ID:
        if (view->wlr_xwayland_surface->parent) {
            return view->wlr_xwayland_surface->parent->window_id;
        }
        return 0;
    case VIEW_PROP_WINDOW_TYPE:
        if (view->wlr_xwayland_surface->window_type_len == 0) {
            return 0;
        }
        return view->wlr_xwayland_surface->window_type[0];
    default:
        return 0;
    }
}

static uint32_t
configure(
    struct hayward_view *view, double lx, double ly, int width, int height
) {
    struct hayward_xwayland_view *xwayland_view = xwayland_view_from_view(view);
    if (xwayland_view == NULL) {
        return 0;
    }
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;

    wlr_xwayland_surface_configure(xsurface, lx, ly, width, height);

    // xwayland doesn't give us a serial for the configure
    return 0;
}

static void
set_activated(struct hayward_view *view, bool activated) {
    if (xwayland_view_from_view(view) == NULL) {
        return;
    }
    struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;

    if (activated && surface->minimized) {
        wlr_xwayland_surface_set_minimized(surface, false);
    }

    wlr_xwayland_surface_activate(surface, activated);
    wlr_xwayland_surface_restack(surface, NULL, XCB_STACK_MODE_ABOVE);
}

static void
set_tiled(struct hayward_view *view, bool tiled) {
    if (xwayland_view_from_view(view) == NULL) {
        return;
    }
    struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
    wlr_xwayland_surface_set_maximized(surface, tiled);
}

static void
set_fullscreen(struct hayward_view *view, bool fullscreen) {
    if (xwayland_view_from_view(view) == NULL) {
        return;
    }
    struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
    wlr_xwayland_surface_set_fullscreen(surface, fullscreen);
}

static bool
wants_floating(struct hayward_view *view) {
    if (xwayland_view_from_view(view) == NULL) {
        return false;
    }
    struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
    struct hayward_xwayland *xwayland = &server.xwayland;

    if (surface->modal) {
        return true;
    }

    for (size_t i = 0; i < surface->window_type_len; ++i) {
        xcb_atom_t type = surface->window_type[i];
        if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_DIALOG] ||
            type == xwayland->atoms[NET_WM_WINDOW_TYPE_UTILITY] ||
            type == xwayland->atoms[NET_WM_WINDOW_TYPE_TOOLBAR] ||
            type == xwayland->atoms[NET_WM_WINDOW_TYPE_SPLASH]) {
            return true;
        }
    }

    xcb_size_hints_t *size_hints = surface->size_hints;
    if (size_hints != NULL && size_hints->min_width > 0 &&
        size_hints->min_height > 0 &&
        (size_hints->max_width == size_hints->min_width ||
         size_hints->max_height == size_hints->min_height)) {
        return true;
    }

    return false;
}

static void
handle_set_decorations(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_decorations);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;

    bool csd = xsurface->decorations != WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
    view_update_csd_from_client(view, csd);
}

static bool
is_transient_for(struct hayward_view *child, struct hayward_view *ancestor) {
    if (xwayland_view_from_view(child) == NULL) {
        return false;
    }
    struct wlr_xwayland_surface *surface = child->wlr_xwayland_surface;
    while (surface) {
        if (surface->parent == ancestor->wlr_xwayland_surface) {
            return true;
        }
        surface = surface->parent;
    }
    return false;
}

static void
_close(struct hayward_view *view) {
    if (xwayland_view_from_view(view) == NULL) {
        return;
    }
    wlr_xwayland_surface_close(view->wlr_xwayland_surface);
}

static void
destroy(struct hayward_view *view) {
    struct hayward_xwayland_view *xwayland_view = xwayland_view_from_view(view);
    if (xwayland_view == NULL) {
        return;
    }
    free(xwayland_view);
}

static void
get_constraints(
    struct hayward_view *view, double *min_width, double *max_width,
    double *min_height, double *max_height
) {
    struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
    xcb_size_hints_t *size_hints = surface->size_hints;

    if (size_hints == NULL) {
        *min_width = DBL_MIN;
        *max_width = DBL_MAX;
        *min_height = DBL_MIN;
        *max_height = DBL_MAX;
        return;
    }

    *min_width = size_hints->min_width > 0 ? size_hints->min_width : DBL_MIN;
    *max_width = size_hints->max_width > 0 ? size_hints->max_width : DBL_MAX;
    *min_height = size_hints->min_height > 0 ? size_hints->min_height : DBL_MIN;
    *max_height = size_hints->max_height > 0 ? size_hints->max_height : DBL_MAX;
}

static const struct hayward_view_impl view_impl = {
    .get_constraints = get_constraints,
    .get_string_prop = get_string_prop,
    .get_int_prop = get_int_prop,
    .configure = configure,
    .set_activated = set_activated,
    .set_tiled = set_tiled,
    .set_fullscreen = set_fullscreen,
    .wants_floating = wants_floating,
    .is_transient_for = is_transient_for,
    .close = _close,
    .destroy = destroy,
};

static void
get_geometry(struct hayward_view *view, struct wlr_box *box) {
    box->x = box->y = 0;
    if (view->surface) {
        box->width = view->surface->current.width;
        box->height = view->surface->current.height;
    } else {
        box->width = 0;
        box->height = 0;
    }
}

static void
view_notify_ready_by_geometry(
    struct hayward_view *view, double x, double y, int width, int height
) {
    struct hayward_window *window = view->window;
    struct hayward_window_state *state = &window->committed;

    if (!window->is_configuring) {
        return;
    }
    if (window->configure_serial != 0) {
        return;
    }

    if ((int)state->content_x != (int)x || (int)state->content_y != (int)y ||
        state->content_width != width || state->content_height != height) {
        return;
    }
    transaction_release();
}

static void
handle_commit(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, commit);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    struct wlr_surface_state *state = &xsurface->surface->current;

    struct wlr_box new_geo;
    get_geometry(view, &new_geo);
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

    view_notify_ready_by_geometry(
        view, xsurface->x, xsurface->y, state->width, state->height
    );
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, destroy);
    struct hayward_view *view = &xwayland_view->view;

    if (view->surface) {
        view_unmap(view);
        wl_list_remove(&xwayland_view->commit.link);
    }

    xwayland_view->view.wlr_xwayland_surface = NULL;

    wl_list_remove(&xwayland_view->destroy.link);
    wl_list_remove(&xwayland_view->request_configure.link);
    wl_list_remove(&xwayland_view->request_fullscreen.link);
    wl_list_remove(&xwayland_view->request_minimize.link);
    wl_list_remove(&xwayland_view->request_move.link);
    wl_list_remove(&xwayland_view->request_resize.link);
    wl_list_remove(&xwayland_view->request_activate.link);
    wl_list_remove(&xwayland_view->set_title.link);
    wl_list_remove(&xwayland_view->set_class.link);
    wl_list_remove(&xwayland_view->set_role.link);
    wl_list_remove(&xwayland_view->set_window_type.link);
    wl_list_remove(&xwayland_view->set_hints.link);
    wl_list_remove(&xwayland_view->set_decorations.link);
    wl_list_remove(&xwayland_view->map.link);
    wl_list_remove(&xwayland_view->unmap.link);
    wl_list_remove(&xwayland_view->override_redirect.link);
    view_begin_destroy(&xwayland_view->view);
}

static void
handle_unmap(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, unmap);
    struct hayward_view *view = &xwayland_view->view;

    hayward_assert(view->surface, "Cannot unmap unmapped view");

    view_unmap(view);

    wl_list_remove(&xwayland_view->commit.link);
}

static void
handle_map(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, map);
    struct wlr_xwayland_surface *xsurface = data;
    struct hayward_view *view = &xwayland_view->view;

    view->natural_width = xsurface->width;
    view->natural_height = xsurface->height;

    // Wire up the commit listener here, because xwayland map/unmap can change
    // the underlying wlr_surface
    wl_signal_add(&xsurface->surface->events.commit, &xwayland_view->commit);
    xwayland_view->commit.notify = handle_commit;

    // Put it back into the tree
    view_map(view, xsurface->surface, xsurface->fullscreen, NULL, false);

    transaction_flush();
}

static void
handle_override_redirect(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, override_redirect);
    struct wlr_xwayland_surface *xsurface = data;
    struct hayward_view *view = &xwayland_view->view;

    bool mapped = xsurface->mapped;
    if (mapped) {
        handle_unmap(&xwayland_view->unmap, NULL);
    }

    handle_destroy(&xwayland_view->destroy, view);
    xsurface->data = NULL;
    struct hayward_xwayland_unmanaged *unmanaged = create_unmanaged(xsurface);
    if (mapped) {
        unmanaged_handle_map(&unmanaged->map, xsurface);
    }
}

static void
handle_request_configure(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_configure);
    struct wlr_xwayland_surface_configure_event *ev = data;
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        wlr_xwayland_surface_configure(
            xsurface, ev->x, ev->y, ev->width, ev->height
        );
        return;
    }
    if (window_is_floating(view->window)) {
        // Respect minimum and maximum sizes
        view->natural_width = ev->width;
        view->natural_height = ev->height;
        window_floating_resize_and_center(view->window);

        configure(
            view, view->window->pending.content_x,
            view->window->pending.content_y,
            view->window->pending.content_width,
            view->window->pending.content_height
        );
        window_set_dirty(view->window);
    } else {
        configure(
            view, view->window->current.content_x,
            view->window->current.content_y,
            view->window->current.content_width,
            view->window->current.content_height
        );
    }
}

static void
handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_fullscreen);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }
    window_set_fullscreen(view->window, xsurface->fullscreen);

    arrange_root(root);
    transaction_flush();
}

static void
handle_request_minimize(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_minimize);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }

    struct wlr_xwayland_minimize_event *e = data;
    bool focused = root_get_focused_window(root) == view->window;
    wlr_xwayland_surface_set_minimized(xsurface, !focused && e->minimize);
}

static void
handle_request_move(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_move);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }
    if (!window_is_floating(view->window) || view->window->pending.fullscreen) {
        return;
    }
    struct hayward_seat *seat = input_manager_current_seat();
    seatop_begin_move_floating(seat, view->window);
}

static void
handle_request_resize(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_resize);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }
    if (!window_is_floating(view->window)) {
        return;
    }
    struct wlr_xwayland_resize_event *e = data;
    struct hayward_seat *seat = input_manager_current_seat();
    seatop_begin_resize_floating(seat, view->window, e->edges);
}

static void
handle_request_activate(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, request_activate);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }
    view_request_activate(view);

    transaction_flush();
}

static void
handle_set_title(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_title);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }
    view_update_title(view, false);
}

static void
handle_set_class(struct wl_listener *listener, void *data) {
    // TODO probably remove.
}

static void
handle_set_role(struct wl_listener *listener, void *data) {
    // TODO probably remove.
}

static void
handle_set_window_type(struct wl_listener *listener, void *data) {
    // TODO probably remove.
}

static void
handle_set_hints(struct wl_listener *listener, void *data) {
    struct hayward_xwayland_view *xwayland_view =
        wl_container_of(listener, xwayland_view, set_hints);
    struct hayward_view *view = &xwayland_view->view;
    struct wlr_xwayland_surface *xsurface = view->wlr_xwayland_surface;
    if (!xsurface->mapped) {
        return;
    }
    const bool hints_urgency = xcb_icccm_wm_hints_get_urgency(xsurface->hints);
    if (!hints_urgency && view->urgent_timer) {
        // The view is in the timeout period. We'll ignore the request to
        // unset urgency so that the view remains urgent until the timer clears
        // it.
        return;
    }
    if (view->allow_request_urgent) {
        view_set_urgent(view, hints_urgency);
    }
}

struct hayward_view *
view_from_wlr_xwayland_surface(struct wlr_xwayland_surface *xsurface) {
    return xsurface->data;
}

struct hayward_xwayland_view *
create_xwayland_view(struct wlr_xwayland_surface *xsurface) {
    hayward_log(
        HAYWARD_DEBUG, "New xwayland surface title='%s' class='%s'",
        xsurface->title, xsurface->class
    );

    struct hayward_xwayland_view *xwayland_view =
        calloc(1, sizeof(struct hayward_xwayland_view));
    hayward_assert(xwayland_view, "Failed to allocate view");

    view_init(&xwayland_view->view, HAYWARD_VIEW_XWAYLAND, &view_impl);
    xwayland_view->view.wlr_xwayland_surface = xsurface;

    wl_signal_add(&xsurface->events.destroy, &xwayland_view->destroy);
    xwayland_view->destroy.notify = handle_destroy;

    wl_signal_add(
        &xsurface->events.request_configure, &xwayland_view->request_configure
    );
    xwayland_view->request_configure.notify = handle_request_configure;

    wl_signal_add(
        &xsurface->events.request_fullscreen, &xwayland_view->request_fullscreen
    );
    xwayland_view->request_fullscreen.notify = handle_request_fullscreen;

    wl_signal_add(
        &xsurface->events.request_minimize, &xwayland_view->request_minimize
    );
    xwayland_view->request_minimize.notify = handle_request_minimize;

    wl_signal_add(
        &xsurface->events.request_activate, &xwayland_view->request_activate
    );
    xwayland_view->request_activate.notify = handle_request_activate;

    wl_signal_add(&xsurface->events.request_move, &xwayland_view->request_move);
    xwayland_view->request_move.notify = handle_request_move;

    wl_signal_add(
        &xsurface->events.request_resize, &xwayland_view->request_resize
    );
    xwayland_view->request_resize.notify = handle_request_resize;

    wl_signal_add(&xsurface->events.set_title, &xwayland_view->set_title);
    xwayland_view->set_title.notify = handle_set_title;

    wl_signal_add(&xsurface->events.set_class, &xwayland_view->set_class);
    xwayland_view->set_class.notify = handle_set_class;

    wl_signal_add(&xsurface->events.set_role, &xwayland_view->set_role);
    xwayland_view->set_role.notify = handle_set_role;

    wl_signal_add(
        &xsurface->events.set_window_type, &xwayland_view->set_window_type
    );
    xwayland_view->set_window_type.notify = handle_set_window_type;

    wl_signal_add(&xsurface->events.set_hints, &xwayland_view->set_hints);
    xwayland_view->set_hints.notify = handle_set_hints;

    wl_signal_add(
        &xsurface->events.set_decorations, &xwayland_view->set_decorations
    );
    xwayland_view->set_decorations.notify = handle_set_decorations;

    wl_signal_add(&xsurface->events.unmap, &xwayland_view->unmap);
    xwayland_view->unmap.notify = handle_unmap;

    wl_signal_add(&xsurface->events.map, &xwayland_view->map);
    xwayland_view->map.notify = handle_map;

    wl_signal_add(
        &xsurface->events.set_override_redirect,
        &xwayland_view->override_redirect
    );
    xwayland_view->override_redirect.notify = handle_override_redirect;

    xsurface->data = xwayland_view;

    return xwayland_view;
}

void
handle_xwayland_surface(struct wl_listener *listener, void *data) {
    struct wlr_xwayland_surface *xsurface = data;

    if (xsurface->override_redirect) {
        hayward_log(HAYWARD_DEBUG, "New xwayland unmanaged surface");
        create_unmanaged(xsurface);
        return;
    }

    create_xwayland_view(xsurface);
}

void
handle_xwayland_ready(struct wl_listener *listener, void *data) {
    struct hayward_server *server =
        wl_container_of(listener, server, xwayland_ready);
    struct hayward_xwayland *xwayland = &server->xwayland;

    xcb_connection_t *xcb_conn = xcb_connect(NULL, NULL);
    int err = xcb_connection_has_error(xcb_conn);
    if (err) {
        hayward_log(HAYWARD_ERROR, "XCB connect failed: %d", err);
        return;
    }

    xcb_intern_atom_cookie_t cookies[ATOM_LAST];
    for (size_t i = 0; i < ATOM_LAST; i++) {
        cookies[i] =
            xcb_intern_atom(xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
    }
    for (size_t i = 0; i < ATOM_LAST; i++) {
        xcb_generic_error_t *error = NULL;
        xcb_intern_atom_reply_t *reply =
            xcb_intern_atom_reply(xcb_conn, cookies[i], &error);
        if (reply != NULL && error == NULL) {
            xwayland->atoms[i] = reply->atom;
        }
        free(reply);

        if (error != NULL) {
            hayward_log(
                HAYWARD_ERROR, "could not resolve atom %s, X11 error code %d",
                atom_map[i], error->error_code
            );
            free(error);
            break;
        }
    }

    xcb_disconnect(xcb_conn);
}
