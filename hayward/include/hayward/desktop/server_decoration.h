#ifndef HWD_DESKTOP_SERVER_DECORATION_H
#define HWD_DESKTOP_SERVER_DECORATION_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_server_decoration.h>

struct hwd_server_decoration {
    struct wlr_server_decoration *wlr_server_decoration;
    struct wl_list link;

    struct wl_listener destroy;
    struct wl_listener mode;
};

struct hwd_server_decoration_manager {
    struct wlr_server_decoration_manager *server_decoration_manager;

    struct wl_list decorations; // hwd_server_decoration::link

    struct wl_listener new_decoration;
};

struct hwd_server_decoration_manager *
hwd_server_decoration_manager_create(struct wl_display *wl_display);

struct hwd_server_decoration *
decoration_from_surface(struct hwd_server_decoration_manager *manager, struct wlr_surface *surface);

#endif
