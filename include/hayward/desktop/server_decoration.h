#ifndef HWD_DESKTOP_SERVER_DECORATION_H
#define HWD_DESKTOP_SERVER_DECORATION_H

#include <wayland-server-core.h>

#include <wlr/types/wlr_server_decoration.h>

struct hwd_server_decoration_manager {
    struct wlr_server_decoration_manager *server_decoration_manager;
};

struct hwd_server_decoration_manager *
hwd_server_decoration_manager_create(struct wl_display *wl_display);

#endif
