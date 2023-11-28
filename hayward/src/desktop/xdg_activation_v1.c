#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/desktop/xdg_activation_v1.h"

#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <hayward/globals/root.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>

static void
handle_request_activate(struct wl_listener *listener, void *data) {
    const struct wlr_xdg_activation_v1_request_activate_event *event = data;

    struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(event->surface);
    if (xdg_surface == NULL) {
        return;
    }

    struct hwd_view *view = xdg_surface->data;
    if (!xdg_surface->surface->mapped) {
        return;
    }
    if (view == NULL) {
        return;
    }

    view_request_activate(view);
    root_commit_focus(root);
}

struct hwd_xdg_activation_v1 *
hwd_xdg_activation_v1_create(struct wl_display *wl_display) {
    struct hwd_xdg_activation_v1 *xdg_activation = calloc(1, sizeof(struct hwd_xdg_activation_v1));
    if (!xdg_activation) {
        return NULL;
    }

    xdg_activation->xdg_activation_v1 = wlr_xdg_activation_v1_create(wl_display);
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
