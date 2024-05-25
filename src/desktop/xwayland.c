#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/desktop/xwayland.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xproto.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/xwayland/xwayland.h>

#include <hayward/globals/root.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_move.h>
#include <hayward/input/seatop_resize_floating.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

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

static void
surface_scene_marker_destroy(struct wlr_addon *addon) {
    // Intentionally left blank.
}

static const struct wlr_addon_interface unmanaged_surface_scene_marker_interface = {
    .name = "hwd_xwayland_unmanaged", .destroy = surface_scene_marker_destroy
};

static void
hwd_xwayland_unmanaged_handle_xsurface_request_configure(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self =
        wl_container_of(listener, self, xsurface_request_configure);
    struct wlr_xwayland_surface_configure_event *ev = data;

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    wlr_xwayland_surface_configure(xsurface, ev->x, ev->y, ev->width, ev->height);
}

static void
hwd_xwayland_unmanaged_handle_xsurface_set_geometry(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self = wl_container_of(listener, self, xsurface_set_geometry);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    wlr_scene_node_set_position(&self->surface_scene->buffer->node, xsurface->x, xsurface->y);
}

static void
hwd_xwayland_unmanaged_handle_xsurface_map(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self = wl_container_of(listener, self, xsurface_map);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    self->surface_scene = wlr_scene_surface_create(root->layers.unmanaged, xsurface->surface);

    if (self->surface_scene) {
        wlr_addon_init(
            &self->surface_scene_marker, &self->surface_scene->buffer->node.addons,
            &unmanaged_surface_scene_marker_interface, &unmanaged_surface_scene_marker_interface
        );

        wlr_scene_node_set_position(&self->surface_scene->buffer->node, xsurface->x, xsurface->y);

        wl_signal_add(&xsurface->events.set_geometry, &self->xsurface_set_geometry);
        self->xsurface_set_geometry.notify = hwd_xwayland_unmanaged_handle_xsurface_set_geometry;
    }

    if (wlr_xwayland_or_surface_wants_focus(xsurface)) {
        struct hwd_seat *seat = input_manager_current_seat();
        struct wlr_xwayland *xwayland = self->xwayland->xwayland;

        wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
        root_set_focused_surface(root, xsurface->surface);
    }
    root_commit_focus(root);
}

static void
hwd_xwayland_unmanaged_handle_xsurface_unmap(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self = wl_container_of(listener, self, xsurface_unmap);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    if (self->surface_scene) {
        wlr_addon_finish(&self->surface_scene_marker);
        wl_list_remove(&self->xsurface_set_geometry.link);

        wlr_scene_node_destroy(&self->surface_scene->buffer->node);
        self->surface_scene = NULL;
    }

    struct hwd_seat *seat = input_manager_current_seat();
    if (seat->wlr_seat->keyboard_state.focused_surface == xsurface->surface) {
        // This simply returns focus to the parent surface if there's one
        // available. This seems to handle JetBrains issues.
        if (xsurface->parent && xsurface->parent->surface &&
            wlr_xwayland_or_surface_wants_focus(xsurface->parent)) {

            root_set_focused_surface(root, xsurface->parent->surface);
        }
    }
    root_commit_focus(root);
}

static void
hwd_xwayland_unmanaged_handle_xsurface_associate(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self = wl_container_of(listener, self, xsurface_associate);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    wl_signal_add(&xsurface->surface->events.map, &self->xsurface_map);
    self->xsurface_map.notify = hwd_xwayland_unmanaged_handle_xsurface_map;

    wl_signal_add(&xsurface->surface->events.unmap, &self->xsurface_unmap);
    self->xsurface_unmap.notify = hwd_xwayland_unmanaged_handle_xsurface_unmap;
}

static void
hwd_xwayland_unmanaged_handle_xsurface_dissociate(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self = wl_container_of(listener, self, xsurface_dissociate);

    wl_list_remove(&self->xsurface_map.link);
    wl_list_remove(&self->xsurface_unmap.link);
}

static void
hwd_xwayland_unmanaged_handle_xsurface_request_activate(struct wl_listener *listener, void *data) {
    struct wlr_xwayland_surface *xsurface = data;

    if (!xsurface->surface->mapped) {
        return;
    }

    root_set_focused_surface(root, xsurface->surface);
    root_commit_focus(root);
}

static void
hwd_xwayland_unmanaged_handle_xsurface_destroy(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self = wl_container_of(listener, self, xsurface_destroy);

    wl_list_remove(&self->xsurface_request_configure.link);
    wl_list_remove(&self->xsurface_associate.link);
    wl_list_remove(&self->xsurface_dissociate.link);
    wl_list_remove(&self->xsurface_destroy.link);
    wl_list_remove(&self->xsurface_override_redirect.link);
    wl_list_remove(&self->xsurface_request_activate.link);
    free(self);
}

static void
hwd_xwayland_view_handle_xsurface_map(struct wl_listener *listener, void *data);

static struct hwd_xwayland_view *
create_xwayland_view(struct hwd_xwayland *xwayland, struct wlr_xwayland_surface *xsurface);

static void
hwd_xwayland_unmanaged_handle_xsurface_override_redirect(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_unmanaged *self =
        wl_container_of(listener, self, xsurface_override_redirect);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    bool mapped = xsurface->surface != NULL && xsurface->surface->mapped;
    if (mapped) {
        hwd_xwayland_unmanaged_handle_xsurface_unmap(&self->xsurface_unmap, NULL);
    }

    hwd_xwayland_unmanaged_handle_xsurface_destroy(&self->xsurface_destroy, NULL);
    xsurface->data = NULL;
    struct hwd_xwayland_view *xwayland_view = create_xwayland_view(self->xwayland, xsurface);
    if (mapped) {
        hwd_xwayland_view_handle_xsurface_map(&xwayland_view->xsurface_map, xsurface);
    }
}

static struct hwd_xwayland_unmanaged *
hwd_xwayland_create_unmanaged(
    struct hwd_xwayland *xwayland, struct wlr_xwayland_surface *xsurface
) {
    struct hwd_xwayland_unmanaged *self = calloc(1, sizeof(struct hwd_xwayland_unmanaged));
    if (self == NULL) {
        wlr_log(WLR_ERROR, "Allocation failed");
        return NULL;
    }

    self->wlr_xwayland_surface = xsurface;
    self->xwayland = xwayland;

    wl_signal_add(&xsurface->events.request_configure, &self->xsurface_request_configure);
    self->xsurface_request_configure.notify =
        hwd_xwayland_unmanaged_handle_xsurface_request_configure;
    wl_signal_add(&xsurface->events.associate, &self->xsurface_associate);
    self->xsurface_associate.notify = hwd_xwayland_unmanaged_handle_xsurface_associate;
    wl_signal_add(&xsurface->events.dissociate, &self->xsurface_dissociate);
    self->xsurface_dissociate.notify = hwd_xwayland_unmanaged_handle_xsurface_dissociate;
    wl_signal_add(&xsurface->events.destroy, &self->xsurface_destroy);
    self->xsurface_destroy.notify = hwd_xwayland_unmanaged_handle_xsurface_destroy;
    wl_signal_add(&xsurface->events.set_override_redirect, &self->xsurface_override_redirect);
    self->xsurface_override_redirect.notify =
        hwd_xwayland_unmanaged_handle_xsurface_override_redirect;
    wl_signal_add(&xsurface->events.request_activate, &self->xsurface_request_activate);
    self->xsurface_request_activate.notify =
        hwd_xwayland_unmanaged_handle_xsurface_request_activate;

    return self;
}

static struct hwd_xwayland_view *
hwd_xwayland_view_from_view(struct hwd_view *view) {
    assert(view->type == HWD_VIEW_XWAYLAND);
    return (struct hwd_xwayland_view *)view;
}

static void
hwd_xwayland_view_handle_window_commit(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, window_commit);
    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    struct hwd_window *window = view->window;

    if (!window_is_visible(window)) {
        return;
    }

    bool dirty = false;

    bool tiled = window_is_tiling(window);
    if (tiled != self->configured_is_tiled) {
        self->configured_is_tiled = tiled;

        wlr_xwayland_surface_set_maximized(xsurface, tiled);

        dirty = true;
    }

    bool fullscreen = window_is_fullscreen(window);
    if (fullscreen != self->configured_is_fullscreen) {
        self->configured_is_fullscreen = fullscreen;

        wlr_xwayland_surface_set_fullscreen(xsurface, fullscreen);

        dirty = true;
    }

    int x = (int)window->pending.content_x;
    int y = (int)window->pending.content_y;
    int width = (int)window->pending.content_width;
    int height = (int)window->pending.content_height;
    if (x != self->configured_x || y != self->configured_y || width != self->configured_width ||
        height != self->configured_height) {
        self->configured_x = x;
        self->configured_y = y;
        self->configured_width = width;
        self->configured_height = height;

        wlr_xwayland_surface_configure(xsurface, x, y, width, height);

        dirty = true;
    }

    if (dirty) {
        window_begin_configure(view->window);
        view_send_frame_done(view);
    }
}

static void
hwd_xwayland_view_handle_window_close(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, window_close);

    wlr_xwayland_surface_close(self->wlr_xwayland_surface);
}

static void
set_activated(struct hwd_view *view, bool activated) {
    struct hwd_xwayland_view *self = hwd_xwayland_view_from_view(view);
    struct wlr_xwayland_surface *surface = self->wlr_xwayland_surface;

    if (activated && surface->minimized) {
        wlr_xwayland_surface_set_minimized(surface, false);
    }

    wlr_xwayland_surface_activate(surface, activated);
    wlr_xwayland_surface_restack(surface, NULL, XCB_STACK_MODE_ABOVE);
}

static bool
wants_floating(struct hwd_xwayland_view *self) {
    struct wlr_xwayland_surface *surface = self->wlr_xwayland_surface;
    struct hwd_xwayland *xwayland = self->xwayland;

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
    if (size_hints != NULL && size_hints->min_width > 0 && size_hints->min_height > 0 &&
        (size_hints->max_width == size_hints->min_width ||
         size_hints->max_height == size_hints->min_height)) {
        return true;
    }

    return false;
}

static void
destroy(struct hwd_view *view) {
    struct hwd_xwayland_view *self = hwd_xwayland_view_from_view(view);
    free(self);
}

static const struct hwd_view_impl view_impl = {
    .set_activated = set_activated,
    .destroy = destroy,
};

static void
get_geometry(struct hwd_view *view, struct wlr_box *box) {
    box->x = box->y = 0;
    if (view->surface) {
        box->width = view->surface->current.width;
        box->height = view->surface->current.height;
    } else {
        box->width = 0;
        box->height = 0;
    }
}

static bool
view_notify_ready_by_geometry(struct hwd_view *view, double x, double y, int width, int height) {
    struct hwd_window *window = view->window;
    struct hwd_window_state *state = &window->committed;

    if (!window->is_configuring) {
        return false;
    }

    if ((int)state->content_x != (int)x || (int)state->content_y != (int)y ||
        state->content_width != width || state->content_height != height) {
        return false;
    }

    window_end_configure(window);

    return true;
}

static void
hwd_xwayland_view_handle_xsurface_commit(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_commit);

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    struct wlr_surface_state *state = &xsurface->surface->current;

    struct wlr_box new_geo;
    get_geometry(view, &new_geo);
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
            struct hwd_window *window = view->window;
            window->floating_width = view->geometry.width;
            window->floating_height = view->geometry.height;
            window_set_dirty(window);
        } else {
            view_center_surface(view);
        }
    }

    bool success =
        view_notify_ready_by_geometry(view, xsurface->x, xsurface->y, state->width, state->height);

    // TODO don't send if transaction is in progress.
    if (!success) {
        view_send_frame_done(view);
    }
}

static void
hwd_xwayland_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_destroy);

    struct hwd_view *view = &self->view;

    assert(view->surface == NULL);
    if (view->surface) {
        //        view_unmap(view);
        wl_list_remove(&self->xsurface_commit.link);
    }

    self->wlr_xwayland_surface = NULL;

    wl_list_remove(&self->xsurface_destroy.link);
    wl_list_remove(&self->xsurface_request_configure.link);
    wl_list_remove(&self->xsurface_request_fullscreen.link);
    wl_list_remove(&self->xsurface_request_minimize.link);
    wl_list_remove(&self->xsurface_request_move.link);
    wl_list_remove(&self->xsurface_request_resize.link);
    wl_list_remove(&self->xsurface_request_activate.link);
    wl_list_remove(&self->xsurface_set_title.link);
    wl_list_remove(&self->xsurface_set_class.link);
    wl_list_remove(&self->xsurface_set_role.link);
    wl_list_remove(&self->xsurface_set_window_type.link);
    wl_list_remove(&self->xsurface_set_hints.link);
    wl_list_remove(&self->xsurface_set_parent.link);
    wl_list_remove(&self->xsurface_associate.link);
    wl_list_remove(&self->xsurface_dissociate.link);
    wl_list_remove(&self->xsurface_override_redirect.link);
    view_begin_destroy(&self->view);
}

static void
hwd_xwayland_view_handle_unmap(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_unmap);

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

    wl_list_remove(&self->xsurface_commit.link);
    wl_list_remove(&self->window_commit.link);
    wl_list_remove(&self->window_close.link);
}

static bool
should_focus(struct hwd_xwayland_view *self) {
    struct hwd_view *view = &self->view;
    struct hwd_workspace *active_workspace = root_get_active_workspace(root);
    struct hwd_workspace *map_workspace = view->window->workspace;
    struct hwd_output *map_output = window_get_output(view->window);
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

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

    if (wlr_xwayland_icccm_input_model(xsurface) == WLR_ICCCM_INPUT_MODEL_NONE) {
        return false;
    }

    return true;
}

static void
hwd_xwayland_view_handle_xsurface_map(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_map);

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    // Wire up the commit listener here, because xwayland map/unmap can change
    // the underlying wlr_surface
    wl_signal_add(&xsurface->surface->events.commit, &self->xsurface_commit);
    self->xsurface_commit.notify = hwd_xwayland_view_handle_xsurface_commit;

    assert(view->surface == NULL);
    view->surface = xsurface->surface;
    view->window = window_create(root, view);

    self->window_commit.notify = hwd_xwayland_view_handle_window_commit;
    wl_signal_add(&view->window->events.commit, &self->window_commit);

    self->window_close.notify = hwd_xwayland_view_handle_window_close;
    wl_signal_add(&view->window->events.close, &self->window_close);

    struct hwd_workspace *workspace = root_get_active_workspace(root);
    assert(workspace != NULL);

    struct hwd_output *output = root_get_active_output(root);
    assert(output != NULL);

    window_set_natural_size(view->window, xsurface->width, xsurface->height);

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

    if (xsurface->fullscreen) {
        // Fullscreen windows still have to have a place as regular
        // tiling or floating windows, so this does not make the
        // previous logic unnecessary.
        window_fullscreen_on_output(view->window, output);
    }

    if (should_focus(self)) {
        root_set_focused_window(root, view->window);
    }

    char const *title = self->wlr_xwayland_surface->title;
    if (title == NULL) {
        title = "";
    }
    window_set_title(view->window, title);

    self->surface_scene =
        wlr_scene_subsurface_tree_create(view->layers.content_tree, xsurface->surface);
    root_commit_focus(root);
}

static void
hwd_xwayland_view_handle_xsurface_override_redirect(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_override_redirect);
    struct wlr_xwayland_surface *xsurface = data;

    struct hwd_view *view = &self->view;

    bool mapped = xsurface->surface != NULL && xsurface->surface->mapped;
    if (mapped) {
        hwd_xwayland_view_handle_unmap(&self->xsurface_unmap, NULL);
    }

    hwd_xwayland_view_handle_destroy(&self->xsurface_destroy, view);
    xsurface->data = NULL;
    struct hwd_xwayland_unmanaged *unmanaged =
        hwd_xwayland_create_unmanaged(self->xwayland, xsurface);
    if (mapped) {
        hwd_xwayland_unmanaged_handle_xsurface_map(&unmanaged->xsurface_map, xsurface);
    }
}

static void
hwd_xwayland_view_handle_xsurface_associate(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_associate);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;

    wl_signal_add(&xsurface->surface->events.map, &self->xsurface_map);
    self->xsurface_map.notify = hwd_xwayland_view_handle_xsurface_map;

    wl_signal_add(&xsurface->surface->events.unmap, &self->xsurface_unmap);
    self->xsurface_unmap.notify = hwd_xwayland_view_handle_unmap;
}

static void
hwd_xwayland_view_handle_xsurface_dissociate(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_dissociate);

    wl_list_remove(&self->xsurface_map.link);
    wl_list_remove(&self->xsurface_unmap.link);
}

static void
hwd_xwayland_view_handle_xsurface_request_configure(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_request_configure);
    struct wlr_xwayland_surface_configure_event *ev = data;

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL || !xsurface->surface->mapped) {
        wlr_xwayland_surface_configure(xsurface, ev->x, ev->y, ev->width, ev->height);
        return;
    }
    if (window_is_floating(view->window)) {
        // Respect minimum and maximum sizes
        window_set_natural_size(view->window, ev->width, ev->height);
        window_floating_resize_and_center(view->window);
    }
    window_set_dirty(view->window);
}

static void
hwd_xwayland_view_handle_xsurface_request_fullscreen(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_request_fullscreen);

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL) {
        return;
    }
    if (!xsurface->surface->mapped) {
        return;
    }

    struct hwd_window *window = view->window;
    struct hwd_output *output = window_get_output(window);

    if (xsurface->fullscreen != window_is_fullscreen(window)) {
        window_fullscreen_on_output(window, output);
    }
}

static void
hwd_xwayland_view_handle_xsurface_request_minimize(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_request_minimize);
    struct wlr_xwayland_minimize_event *e = data;

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL) {
        return;
    }
    if (!xsurface->surface->mapped) {
        return;
    }

    bool focused = root_get_focused_window(root) == view->window;
    wlr_xwayland_surface_set_minimized(xsurface, !focused && e->minimize);
}

static void
hwd_xwayland_view_handle_xsurface_request_move(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_request_move);

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL) {
        return;
    }
    if (!xsurface->surface->mapped) {
        return;
    }
    if (window_is_fullscreen(view->window)) {
        return;
    }
    struct hwd_seat *seat = input_manager_current_seat();
    seatop_begin_move(seat, view->window);
}

static void
hwd_xwayland_view_handle_xsurface_request_resize(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_request_resize);
    struct wlr_xwayland_resize_event *e = data;

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL) {
        return;
    }
    if (!xsurface->surface->mapped) {
        return;
    }
    if (!window_is_floating(view->window)) {
        return;
    }
    struct hwd_seat *seat = input_manager_current_seat();
    seatop_begin_resize_floating(seat, view->window, e->edges);
}

static void
hwd_xwayland_view_handle_xsurface_request_activate(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_request_activate);

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL) {
        return;
    }
    if (!xsurface->surface->mapped) {
        return;
    }

    window_request_activate(view->window);
    root_commit_focus(root);
}

static void
hwd_xwayland_view_handle_xsurface_set_title(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_set_title);

    struct hwd_view *view = &self->view;

    struct hwd_window *window = view->window;
    if (window == NULL) {
        return;
    }

    window_set_title(window, self->wlr_xwayland_surface->title);
}

static void
hwd_xwayland_view_handle_xsurface_set_class(struct wl_listener *listener, void *data) {
    // TODO probably remove.
}

static void
hwd_xwayland_view_handle_xsurface_set_role(struct wl_listener *listener, void *data) {
    // TODO probably remove.
}

static void
hwd_xwayland_view_handle_xsurface_set_window_type(struct wl_listener *listener, void *data) {
    // TODO probably remove.
}

static void
hwd_xwayland_view_handle_xsurface_set_hints(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_set_hints);

    struct hwd_view *view = &self->view;
    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    if (xsurface->surface == NULL) {
        return;
    }
    if (!xsurface->surface->mapped) {
        return;
    }
    const bool hints_urgency = xcb_icccm_wm_hints_get_urgency(xsurface->hints);

    window_set_urgent(view->window, hints_urgency);
}

static void
hwd_xwayland_view_handle_xsurface_set_parent(struct wl_listener *listener, void *data) {
    struct hwd_xwayland_view *self = wl_container_of(listener, self, xsurface_set_parent);

    struct wlr_xwayland_surface *xsurface = self->wlr_xwayland_surface;
    struct hwd_xwayland_view *new_parent = NULL;
    if (xsurface->parent != NULL) {
        new_parent = xsurface->parent->data;
    }

    window_set_transient_for(
        self->view.window, new_parent != NULL ? new_parent->view.window : NULL
    );
}

static struct hwd_xwayland_view *
create_xwayland_view(struct hwd_xwayland *xwayland, struct wlr_xwayland_surface *xsurface) {
    wlr_log(
        WLR_DEBUG, "New xwayland surface title='%s' class='%s'", xsurface->title, xsurface->class
    );

    struct hwd_xwayland_view *self = calloc(1, sizeof(struct hwd_xwayland_view));
    assert(self);

    view_init(&self->view, HWD_VIEW_XWAYLAND, &view_impl);
    self->wlr_xwayland_surface = xsurface;
    self->xwayland = xwayland;

    wl_signal_add(&xsurface->events.destroy, &self->xsurface_destroy);
    self->xsurface_destroy.notify = hwd_xwayland_view_handle_destroy;

    wl_signal_add(&xsurface->events.request_configure, &self->xsurface_request_configure);
    self->xsurface_request_configure.notify = hwd_xwayland_view_handle_xsurface_request_configure;

    wl_signal_add(&xsurface->events.request_fullscreen, &self->xsurface_request_fullscreen);
    self->xsurface_request_fullscreen.notify = hwd_xwayland_view_handle_xsurface_request_fullscreen;

    wl_signal_add(&xsurface->events.request_minimize, &self->xsurface_request_minimize);
    self->xsurface_request_minimize.notify = hwd_xwayland_view_handle_xsurface_request_minimize;

    wl_signal_add(&xsurface->events.request_activate, &self->xsurface_request_activate);
    self->xsurface_request_activate.notify = hwd_xwayland_view_handle_xsurface_request_activate;

    wl_signal_add(&xsurface->events.request_move, &self->xsurface_request_move);
    self->xsurface_request_move.notify = hwd_xwayland_view_handle_xsurface_request_move;

    wl_signal_add(&xsurface->events.request_resize, &self->xsurface_request_resize);
    self->xsurface_request_resize.notify = hwd_xwayland_view_handle_xsurface_request_resize;

    wl_signal_add(&xsurface->events.set_title, &self->xsurface_set_title);
    self->xsurface_set_title.notify = hwd_xwayland_view_handle_xsurface_set_title;

    wl_signal_add(&xsurface->events.set_class, &self->xsurface_set_class);
    self->xsurface_set_class.notify = hwd_xwayland_view_handle_xsurface_set_class;

    wl_signal_add(&xsurface->events.set_role, &self->xsurface_set_role);
    self->xsurface_set_role.notify = hwd_xwayland_view_handle_xsurface_set_role;

    wl_signal_add(&xsurface->events.set_window_type, &self->xsurface_set_window_type);
    self->xsurface_set_window_type.notify = hwd_xwayland_view_handle_xsurface_set_window_type;

    wl_signal_add(&xsurface->events.set_hints, &self->xsurface_set_hints);
    self->xsurface_set_hints.notify = hwd_xwayland_view_handle_xsurface_set_hints;

    wl_signal_add(&xsurface->events.set_parent, &self->xsurface_set_parent);
    self->xsurface_set_parent.notify = hwd_xwayland_view_handle_xsurface_set_parent;

    wl_signal_add(&xsurface->events.dissociate, &self->xsurface_dissociate);
    self->xsurface_dissociate.notify = hwd_xwayland_view_handle_xsurface_dissociate;

    wl_signal_add(&xsurface->events.associate, &self->xsurface_associate);
    self->xsurface_associate.notify = hwd_xwayland_view_handle_xsurface_associate;

    wl_signal_add(&xsurface->events.set_override_redirect, &self->xsurface_override_redirect);
    self->xsurface_override_redirect.notify = hwd_xwayland_view_handle_xsurface_override_redirect;

    xsurface->data = self;

    return self;
}

static void
hwd_xwayland_handle_new_surface(struct wl_listener *listener, void *data) {
    struct hwd_xwayland *self = wl_container_of(listener, self, new_surface);
    struct wlr_xwayland_surface *xsurface = data;

    if (xsurface->override_redirect) {
        wlr_log(WLR_DEBUG, "New xwayland unmanaged surface");
        hwd_xwayland_create_unmanaged(self, xsurface);
        return;
    }

    create_xwayland_view(self, xsurface);
}

static void
hwd_xwayland_handle_ready(struct wl_listener *listener, void *data) {
    struct hwd_xwayland *self = wl_container_of(listener, self, ready);

    xcb_connection_t *xcb_conn = xcb_connect(NULL, NULL);
    int err = xcb_connection_has_error(xcb_conn);
    if (err) {
        wlr_log(WLR_ERROR, "XCB connect failed: %d", err);
        return;
    }

    xcb_intern_atom_cookie_t cookies[ATOM_LAST];
    for (size_t i = 0; i < ATOM_LAST; i++) {
        cookies[i] = xcb_intern_atom(xcb_conn, 0, strlen(atom_map[i]), atom_map[i]);
    }
    for (size_t i = 0; i < ATOM_LAST; i++) {
        xcb_generic_error_t *error = NULL;
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(xcb_conn, cookies[i], &error);
        if (reply != NULL && error == NULL) {
            self->atoms[i] = reply->atom;
        }
        free(reply);

        if (error != NULL) {
            wlr_log(
                WLR_ERROR, "could not resolve atom %s, X11 error code %d", atom_map[i],
                error->error_code
            );
            free(error);
            break;
        }
    }

    xcb_disconnect(xcb_conn);
}

struct hwd_xwayland *
hwd_xwayland_create(struct wl_display *wl_display, struct wlr_compositor *compositor, bool lazy) {
    struct hwd_xwayland *self = calloc(1, sizeof(struct hwd_xwayland));
    if (self == NULL) {
        return NULL;
    }

    self->xwayland = wlr_xwayland_create(wl_display, compositor, lazy);

    if (!self->xwayland) {
        free(self);
        unsetenv("DISPLAY");
        return NULL;
    } else {
        self->new_surface.notify = hwd_xwayland_handle_new_surface;
        wl_signal_add(&self->xwayland->events.new_surface, &self->new_surface);

        self->ready.notify = hwd_xwayland_handle_ready;
        wl_signal_add(&self->xwayland->events.ready, &self->ready);

        setenv("DISPLAY", self->xwayland->display_name, true);

        /* xcursor configured by the default seat */
        return self;
    }
}

void
hwd_xwayland_destroy(struct hwd_xwayland *self) {
    wlr_xwayland_destroy(self->xwayland);
    free(self);
}
