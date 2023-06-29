#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/xdg_decoration.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <hayward/desktop/xdg_shell.h>
#include <hayward/transaction.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#include <config.h>

static void
xdg_decoration_handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_xdg_decoration *deco =
        wl_container_of(listener, deco, destroy);

    transaction_begin();

    if (deco->view) {
        deco->view->xdg_decoration = NULL;
    }
    wl_list_remove(&deco->destroy.link);
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->link);
    free(deco);

    transaction_end();
}

static void
xdg_decoration_handle_request_mode(struct wl_listener *listener, void *data) {
    struct hayward_xdg_decoration *deco =
        wl_container_of(listener, deco, request_mode);

    transaction_begin();

    struct hayward_view *view = deco->view;
    enum wlr_xdg_toplevel_decoration_v1_mode mode =
        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    enum wlr_xdg_toplevel_decoration_v1_mode client_mode =
        deco->wlr_xdg_decoration->requested_mode;

    bool floating;
    if (view->window) {
        floating = window_is_floating(view->window);
        bool csd = false;
        csd = client_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        view_update_csd_from_client(view, csd);
        arrange_window(view->window);
        transaction_end();
    } else {
        floating =
            view->impl->wants_floating && view->impl->wants_floating(view);
    }

    if (floating && client_mode) {
        mode = client_mode;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration, mode);

    transaction_end();
}

static void
handle_new_toplevel_decoration(struct wl_listener *listener, void *data) {
    struct hayward_xdg_decoration_manager *manager =
        wl_container_of(listener, manager, new_toplevel_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;

    transaction_begin();

    struct hayward_xdg_shell_view *xdg_shell_view = wlr_deco->surface->data;

    struct hayward_xdg_decoration *deco = calloc(1, sizeof(*deco));
    if (deco == NULL) {
        transaction_end();
        return;
    }

    deco->view = &xdg_shell_view->view;
    deco->view->xdg_decoration = deco;
    deco->wlr_xdg_decoration = wlr_deco;

    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
    deco->destroy.notify = xdg_decoration_handle_destroy;

    wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);
    deco->request_mode.notify = xdg_decoration_handle_request_mode;

    wl_list_insert(&manager->xdg_decorations, &deco->link);

    xdg_decoration_handle_request_mode(&deco->request_mode, wlr_deco);

    transaction_end();
}

struct hayward_xdg_decoration_manager *
hayward_xdg_decoration_manager_create(struct wl_display *wl_display) {
    struct hayward_xdg_decoration_manager *manager =
        calloc(1, sizeof(struct hayward_xdg_decoration_manager));
    if (manager == NULL) {
        return NULL;
    }

    manager->xdg_decoration_manager =
        wlr_xdg_decoration_manager_v1_create(wl_display);
    if (manager->xdg_decoration_manager == NULL) {
        free(manager);
        return NULL;
    }

    manager->new_toplevel_decoration.notify = handle_new_toplevel_decoration;
    wl_signal_add(
        &manager->xdg_decoration_manager->events.new_toplevel_decoration,
        &manager->new_toplevel_decoration
    );
    wl_list_init(&manager->xdg_decorations);

    return manager;
}
