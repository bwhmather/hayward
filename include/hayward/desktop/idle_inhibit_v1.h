#ifndef HWD_DESKTOP_IDLE_INHIBIT_V1_H
#define HWD_DESKTOP_IDLE_INHIBIT_V1_H

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>

struct hwd_idle_inhibit_manager_v1 {
    struct wlr_idle_inhibit_manager_v1 *wlr_manager;
    struct wl_listener new_idle_inhibitor_v1;
    struct wl_list inhibitors;

    struct wlr_idle_notifier_v1 *idle;
};

struct hwd_idle_inhibitor_v1 {
    struct hwd_idle_inhibit_manager_v1 *manager;
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor;

    struct wl_list link;
    struct wl_listener destroy;
};

void
hwd_idle_inhibit_v1_check_active(struct hwd_idle_inhibit_manager_v1 *manager);

struct hwd_idle_inhibit_manager_v1 *
hwd_idle_inhibit_manager_v1_create(
    struct wl_display *wl_display, struct wlr_idle_notifier_v1 *idle
);

#endif
