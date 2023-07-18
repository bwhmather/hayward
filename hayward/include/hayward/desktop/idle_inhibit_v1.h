#ifndef HWD_DESKTOP_IDLE_INHIBIT_V1_H
#define HWD_DESKTOP_IDLE_INHIBIT_V1_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>

enum hwd_idle_inhibit_mode {
    INHIBIT_IDLE_APPLICATION, // Application set inhibitor (when visible)
    INHIBIT_IDLE_FOCUS,       // User set inhibitor when focused
    INHIBIT_IDLE_FULLSCREEN,  // User set inhibitor when fullscreen + visible
    INHIBIT_IDLE_OPEN,        // User set inhibitor while open
    INHIBIT_IDLE_VISIBLE      // User set inhibitor when visible
};

struct hwd_idle_inhibit_manager_v1 {
    struct wlr_idle_inhibit_manager_v1 *wlr_manager;
    struct wl_listener new_idle_inhibitor_v1;
    struct wl_list inhibitors;

    struct wlr_idle *idle;
};

struct hwd_idle_inhibitor_v1 {
    struct hwd_idle_inhibit_manager_v1 *manager;
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor;
    struct hwd_view *view;
    enum hwd_idle_inhibit_mode mode;

    struct wl_list link;
    struct wl_listener destroy;
};

struct hwd_idle_inhibit_manager_v1 *
hwd_idle_inhibit_manager_v1_create(struct wl_display *wl_display, struct wlr_idle *idle);

void
hwd_idle_inhibit_v1_check_active(struct hwd_idle_inhibit_manager_v1 *manager);

struct hwd_idle_inhibitor_v1 *
hwd_idle_inhibit_v1_application_inhibitor_for_view(struct hwd_view *view);

bool
hwd_idle_inhibit_v1_is_active(struct hwd_idle_inhibitor_v1 *inhibitor);

#endif
