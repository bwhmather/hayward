#ifndef HAYWARD_DESKTOP_SERVER_DECORATION_H
#define HAYWARD_DESKTOP_SERVER_DECORATION_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_server_decoration.h>

struct hayward_server_decoration {
    struct wlr_server_decoration *wlr_server_decoration;
    struct wl_list link;

    struct wl_listener destroy;
    struct wl_listener mode;
};

struct hayward_server_decoration_manager {
    struct wlr_server_decoration_manager *server_decoration_manager;

    struct wl_list decorations; // hayward_server_decoration::link

    struct wl_listener new_decoration;
};

struct hayward_server_decoration_manager *
hayward_server_decoration_manager_create(struct wl_display *wl_display);

struct hayward_server_decoration *
decoration_from_surface(
    struct hayward_server_decoration_manager *manager,
    struct wlr_surface *surface
);

#endif
