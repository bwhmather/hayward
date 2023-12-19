#ifndef HWD_DESKTOP_XDG_DECORATION_H
#define HWD_DESKTOP_XDG_DECORATION_H

#include <wayland-server-core.h>

#include <wlr/types/wlr_xdg_decoration_v1.h>

struct hwd_xdg_decoration_manager {
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;

    struct wl_listener new_toplevel_decoration;
};

struct hwd_xdg_decoration_manager *
hwd_xdg_decoration_manager_create(struct wl_display *wl_display);

#endif
