#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/desktop/idle_inhibit_v1.h"

#include <stdbool.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/util/log.h>

#include <hayward/desktop/xdg_shell.h>
#include <hayward/server.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

static void
handle_idle_inhibitor_v1(struct wl_listener *listener, void *data);

static void
destroy_inhibitor(struct hwd_idle_inhibitor_v1 *inhibitor) {
    wl_list_remove(&inhibitor->link);
    wl_list_remove(&inhibitor->destroy.link);
    hwd_idle_inhibit_v1_check_active(inhibitor->manager);
    free(inhibitor);
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_idle_inhibitor_v1 *inhibitor = wl_container_of(listener, inhibitor, destroy);

    wlr_log(WLR_DEBUG, "Hayward idle inhibitor destroyed");
    destroy_inhibitor(inhibitor);
}

static void
handle_idle_inhibitor_v1(struct wl_listener *listener, void *data) {
    struct hwd_idle_inhibit_manager_v1 *manager =
        wl_container_of(listener, manager, new_idle_inhibitor_v1);
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;

    wlr_log(WLR_DEBUG, "New hayward idle inhibitor");

    struct hwd_idle_inhibitor_v1 *inhibitor = calloc(1, sizeof(struct hwd_idle_inhibitor_v1));
    if (!inhibitor) {
        return;
    }

    inhibitor->manager = manager;
    inhibitor->wlr_inhibitor = wlr_inhibitor;
    wl_list_insert(&manager->inhibitors, &inhibitor->link);

    inhibitor->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

    hwd_idle_inhibit_v1_check_active(manager);
}

static bool
hwd_idle_inhibit_v1_is_active(struct hwd_idle_inhibitor_v1 *inhibitor) {
    struct wlr_surface *wlr_surface = inhibitor->wlr_inhibitor->surface;

    struct hwd_xdg_shell_view *xdg_view = hwd_xdg_shell_view_from_wlr_surface(wlr_surface);
    if (xdg_view == NULL) {
        return false;
    }

    if (xdg_view->view.window == NULL) {
        return false;
    }

    if (!window_is_visible(xdg_view->view.window)) {
        return false;
    }

    return true;
}

void
hwd_idle_inhibit_v1_check_active(struct hwd_idle_inhibit_manager_v1 *manager) {
    struct hwd_idle_inhibitor_v1 *inhibitor;
    bool inhibited = false;
    wl_list_for_each(inhibitor, &manager->inhibitors, link) {
        if ((inhibited = hwd_idle_inhibit_v1_is_active(inhibitor))) {
            break;
        }
    }
    wlr_idle_notifier_v1_set_inhibited(server.idle_notifier_v1, inhibited);
}

struct hwd_idle_inhibit_manager_v1 *
hwd_idle_inhibit_manager_v1_create(
    struct wl_display *wl_display, struct wlr_idle_notifier_v1 *idle
) {
    struct hwd_idle_inhibit_manager_v1 *manager =
        calloc(1, sizeof(struct hwd_idle_inhibit_manager_v1));
    if (!manager) {
        return NULL;
    }

    manager->wlr_manager = wlr_idle_inhibit_v1_create(wl_display);
    if (!manager->wlr_manager) {
        free(manager);
        return NULL;
    }
    manager->idle = idle;
    wl_signal_add(&manager->wlr_manager->events.new_inhibitor, &manager->new_idle_inhibitor_v1);
    manager->new_idle_inhibitor_v1.notify = handle_idle_inhibitor_v1;
    wl_list_init(&manager->inhibitors);

    return manager;
}
