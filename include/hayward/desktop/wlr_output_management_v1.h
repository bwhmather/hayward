#ifndef HWD_DESKTOP_WLR_OUTPUT_MANAGEMENT_V1_H
#define HWD_DESKTOP_WLR_OUTPUT_MANAGEMENT_V1_H

#include <wayland-server-core.h>

#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>

struct hwd_wlr_output_manager_v1 {
    struct wlr_output_manager_v1 *wlr_manager;
    struct wlr_output_layout *output_layout;

    struct wl_listener output_manager_apply;
    struct wl_listener output_manager_test;
    struct wl_listener output_manager_destroy;
    struct wl_listener output_layout_change;
    struct wl_listener output_layout_destroy;

    struct {
        struct wl_signal destroy;
    } events;
};

struct hwd_wlr_output_manager_v1 *
hwd_wlr_output_manager_v1_create(
    struct wl_display *wl_display, struct wlr_output_layout *output_layout
);

#endif
