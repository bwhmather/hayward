#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/idle_inhibit_v1.h"

#include <config.h>

#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>

#include <hayward-common/log.h>

#include <hayward/globals/root.h>
#include <hayward/server.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

static void
handle_idle_inhibitor_v1(struct wl_listener *listener, void *data);

struct hwd_idle_inhibit_manager_v1 *
hwd_idle_inhibit_manager_v1_create(struct wl_display *wl_display, struct wlr_idle *idle) {
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

void
hwd_idle_inhibit_v1_check_active(struct hwd_idle_inhibit_manager_v1 *manager) {
    struct hwd_idle_inhibitor_v1 *inhibitor;
    bool inhibited = false;
    wl_list_for_each(inhibitor, &manager->inhibitors, link) {
        if ((inhibited = hwd_idle_inhibit_v1_is_active(inhibitor))) {
            break;
        }
    }
    wlr_idle_set_enabled(manager->idle, NULL, !inhibited);
}

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

    hwd_log(HWD_DEBUG, "Hayward idle inhibitor destroyed");
    destroy_inhibitor(inhibitor);
}

static void
handle_idle_inhibitor_v1(struct wl_listener *listener, void *data) {
    struct hwd_idle_inhibit_manager_v1 *manager =
        wl_container_of(listener, manager, new_idle_inhibitor_v1);
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;

    hwd_log(HWD_DEBUG, "New hayward idle inhibitor");

    struct hwd_idle_inhibitor_v1 *inhibitor = calloc(1, sizeof(struct hwd_idle_inhibitor_v1));
    if (!inhibitor) {
        return;
    }

    inhibitor->manager = manager;
    inhibitor->mode = INHIBIT_IDLE_APPLICATION;
    inhibitor->wlr_inhibitor = wlr_inhibitor;
    wl_list_insert(&manager->inhibitors, &inhibitor->link);

    inhibitor->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

    hwd_idle_inhibit_v1_check_active(manager);
}

struct hwd_idle_inhibitor_v1 *
hwd_idle_inhibit_v1_application_inhibitor_for_view(struct hwd_view *view) {
    struct hwd_idle_inhibitor_v1 *inhibitor;
    wl_list_for_each(inhibitor, &server.idle_inhibit_manager_v1->inhibitors, link) {
        if (inhibitor->mode == INHIBIT_IDLE_APPLICATION &&
            view_from_wlr_surface(inhibitor->wlr_inhibitor->surface) == view) {
            return inhibitor;
        }
    }
    return NULL;
}

bool
hwd_idle_inhibit_v1_is_active(struct hwd_idle_inhibitor_v1 *inhibitor) {
    switch (inhibitor->mode) {
    case INHIBIT_IDLE_APPLICATION:;
        // If there is no view associated with the inhibitor, assume visible
        struct hwd_view *view = view_from_wlr_surface(inhibitor->wlr_inhibitor->surface);
        return !view || !view->window || view_is_visible(view);
    case INHIBIT_IDLE_FOCUS:;
        struct hwd_window *window = root_get_focused_window(root);
        if (window && window->view == inhibitor->view) {
            return true;
        }
        return false;
    case INHIBIT_IDLE_FULLSCREEN:
        return inhibitor->view->window && window_is_fullscreen(inhibitor->view->window) &&
            view_is_visible(inhibitor->view);
    case INHIBIT_IDLE_OPEN:
        // Inhibitor is destroyed on unmap so it must be open/mapped
        return true;
    case INHIBIT_IDLE_VISIBLE:
        return view_is_visible(inhibitor->view);
    }
    return false;
}
