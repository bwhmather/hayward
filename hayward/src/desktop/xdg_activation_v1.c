#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/desktop/xdg_activation_v1.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <hayward/tree/view.h>

#include <config.h>

static void
handle_request_activate(struct wl_listener *listener, void *data) {
    const struct wlr_xdg_activation_v1_request_activate_event *event = data;

    if (!wlr_surface_is_xdg_surface(event->surface)) {
        return;
    }

    struct wlr_xdg_surface *xdg_surface =
        wlr_xdg_surface_from_wlr_surface(event->surface);
    struct hayward_view *view = xdg_surface->data;
    if (!xdg_surface->mapped || view == NULL) {
        return;
    }

    view_request_activate(view);
}

struct hayward_xdg_activation_v1 *
hayward_xdg_activation_v1_create(struct wl_display *wl_display) {
    struct hayward_xdg_activation_v1 *xdg_activation =
        calloc(1, sizeof(struct hayward_xdg_activation_v1));
    if (!xdg_activation) {
        return NULL;
    }

    xdg_activation->xdg_activation_v1 =
        wlr_xdg_activation_v1_create(wl_display);
    if (xdg_activation->xdg_activation_v1 == NULL) {
        free(xdg_activation);
        return NULL;
    }

    xdg_activation->request_activate.notify = handle_request_activate;
    wl_signal_add(
        &xdg_activation->xdg_activation_v1->events.request_activate,
        &xdg_activation->request_activate
    );

    return xdg_activation;
}
