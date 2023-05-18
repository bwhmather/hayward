#ifndef HAYWARD_XDG_DECORATION_H
#define HAYWARD_XDG_DECORATION_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include <hayward/tree/view.h>

struct hayward_xdg_decoration {
    struct wlr_xdg_toplevel_decoration_v1 *wlr_xdg_decoration;
    struct wl_list link;

    struct hayward_view *view;

    struct wl_listener destroy;
    struct wl_listener request_mode;
};

#endif
