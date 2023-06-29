#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/server_decoration.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_server_decoration.h>

#include <hayward/transaction.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/view.h>

#include <config.h>

static void
server_decoration_handle_destroy(struct wl_listener *listener, void *data) {
    transaction_begin();

    struct hayward_server_decoration *deco =
        wl_container_of(listener, deco, destroy);
    wl_list_remove(&deco->destroy.link);
    wl_list_remove(&deco->mode.link);
    wl_list_remove(&deco->link);
    free(deco);

    transaction_flush();
}

static void
server_decoration_handle_mode(struct wl_listener *listener, void *data) {
    transaction_begin();

    struct hayward_server_decoration *deco =
        wl_container_of(listener, deco, mode);
    struct hayward_view *view =
        view_from_wlr_surface(deco->wlr_server_decoration->surface);
    if (view == NULL || view->surface != deco->wlr_server_decoration->surface) {
        transaction_flush();
        return;
    }

    bool csd = deco->wlr_server_decoration->mode ==
        WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
    view_update_csd_from_client(view, csd);

    arrange_window(view->window);

    transaction_flush();
}

static void
handle_new_decoration(struct wl_listener *listener, void *data) {
    transaction_begin();

    struct hayward_server_decoration_manager *manager =
        wl_container_of(listener, manager, new_decoration);
    struct wlr_server_decoration *wlr_deco = data;

    struct hayward_server_decoration *deco = calloc(1, sizeof(*deco));
    if (deco == NULL) {
        transaction_flush();
        return;
    }

    deco->wlr_server_decoration = wlr_deco;

    wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
    deco->destroy.notify = server_decoration_handle_destroy;

    wl_signal_add(&wlr_deco->events.mode, &deco->mode);
    deco->mode.notify = server_decoration_handle_mode;

    wl_list_insert(&manager->decorations, &deco->link);

    transaction_flush();
}

struct hayward_server_decoration_manager *
hayward_server_decoration_manager_create(struct wl_display *wl_display) {
    struct hayward_server_decoration_manager *manager =
        calloc(1, sizeof(struct hayward_server_decoration_manager));
    if (manager == NULL) {
        return NULL;
    }

    manager->server_decoration_manager =
        wlr_server_decoration_manager_create(wl_display);
    if (manager->server_decoration_manager == NULL) {
        free(manager);
        return NULL;
    }
    wlr_server_decoration_manager_set_default_mode(
        manager->server_decoration_manager,
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );

    wl_list_init(&manager->decorations);

    manager->new_decoration.notify = handle_new_decoration;
    wl_signal_add(
        &manager->server_decoration_manager->events.new_decoration,
        &manager->new_decoration
    );
    return manager;
}

struct hayward_server_decoration *
decoration_from_surface(
    struct hayward_server_decoration_manager *manager,
    struct wlr_surface *surface
) {
    struct hayward_server_decoration *deco;
    wl_list_for_each(deco, &manager->decorations, link) {
        if (deco->wlr_server_decoration->surface == surface) {
            return deco;
        }
    }
    return NULL;
}
