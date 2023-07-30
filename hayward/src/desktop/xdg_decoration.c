#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/xdg_decoration.h"

#include <config.h>

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include <hayward/globals/transaction.h>
#include <hayward/transaction.h>

static void
handle_new_toplevel_decoration(struct wl_listener *listener, void *data) {
    struct hwd_xdg_decoration_manager *manager =
        wl_container_of(listener, manager, new_toplevel_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;

    hwd_transaction_manager_begin_transaction(transaction_manager);

    // THIS IS SPARTA!
    wlr_xdg_toplevel_decoration_v1_set_mode(
        wlr_deco, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
    );

    hwd_transaction_manager_end_transaction(transaction_manager);
}

struct hwd_xdg_decoration_manager *
hwd_xdg_decoration_manager_create(struct wl_display *wl_display) {
    struct hwd_xdg_decoration_manager *manager =
        calloc(1, sizeof(struct hwd_xdg_decoration_manager));
    if (manager == NULL) {
        return NULL;
    }

    manager->xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(wl_display);
    if (manager->xdg_decoration_manager == NULL) {
        free(manager);
        return NULL;
    }

    manager->new_toplevel_decoration.notify = handle_new_toplevel_decoration;
    wl_signal_add(
        &manager->xdg_decoration_manager->events.new_toplevel_decoration,
        &manager->new_toplevel_decoration
    );

    return manager;
}
