#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/desktop/server_decoration.h"

#include <stdlib.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_server_decoration.h>

struct hwd_server_decoration_manager *
hwd_server_decoration_manager_create(struct wl_display *wl_display) {
    struct hwd_server_decoration_manager *manager =
        calloc(1, sizeof(struct hwd_server_decoration_manager));
    if (manager == NULL) {
        return NULL;
    }

    manager->server_decoration_manager = wlr_server_decoration_manager_create(wl_display);
    if (manager->server_decoration_manager == NULL) {
        free(manager);
        return NULL;
    }
    wlr_server_decoration_manager_set_default_mode(
        manager->server_decoration_manager, WLR_SERVER_DECORATION_MANAGER_MODE_SERVER
    );

    return manager;
}
