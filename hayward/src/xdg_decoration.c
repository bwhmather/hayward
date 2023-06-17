#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/xdg_decoration.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <hayward/desktop/xdg_shell.h>
#include <hayward/server.h>
#include <hayward/transaction.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#include <config.h>

static void
xdg_decoration_handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_xdg_decoration *deco =
        wl_container_of(listener, deco, destroy);
    if (deco->view) {
        deco->view->xdg_decoration = NULL;
    }
    wl_list_remove(&deco->destroy.link);
    wl_list_remove(&deco->request_mode.link);
    wl_list_remove(&deco->link);
    free(deco);
}

static void
xdg_decoration_handle_request_mode(struct wl_listener *listener, void *data) {
    struct hayward_xdg_decoration *deco =
        wl_container_of(listener, deco, request_mode);
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
        transaction_flush();
    } else {
        floating =
            view->impl->wants_floating && view->impl->wants_floating(view);
    }

    if (floating && client_mode) {
        mode = client_mode;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration, mode);
}

void
handle_xdg_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
    struct hayward_xdg_shell_view *xdg_shell_view = wlr_deco->surface->data;

    struct hayward_xdg_decoration *deco = calloc(1, sizeof(*deco));
    if (deco == NULL) {
        return;
    }

    deco->view = &xdg_shell_view->view;
    deco->view->xdg_decoration = deco;
    deco->wlr_xdg_decoration = wlr_deco;

    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
    deco->destroy.notify = xdg_decoration_handle_destroy;

    wl_signal_add(&wlr_deco->events.request_mode, &deco->request_mode);
    deco->request_mode.notify = xdg_decoration_handle_request_mode;

    wl_list_insert(&server.xdg_decorations, &deco->link);

    xdg_decoration_handle_request_mode(&deco->request_mode, wlr_deco);
}
