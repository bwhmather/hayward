#ifndef HAYWARD_DESKTOP_XDG_ACTIVATION_V1_H
#define HAYWARD_DESKTOP_XDG_ACTIVATION_V1_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

struct hayward_xdg_activation_v1 {
    struct wlr_xdg_activation_v1 *xdg_activation_v1;

    struct wl_listener request_activate;
};

struct hayward_xdg_activation_v1 *
hayward_xdg_activation_v1_create(struct wl_display *wl_display);

#endif
