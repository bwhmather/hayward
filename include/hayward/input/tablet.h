#ifndef HWD_INPUT_TABLET_H
#define HWD_INPUT_TABLET_H

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>

struct hwd_seat;
struct wlr_tablet_tool;

struct hwd_tablet {
    struct wl_list link;
    struct hwd_seat_device *seat_device;
    struct wlr_tablet_v2_tablet *tablet_v2;
};

enum hwd_tablet_tool_mode {
    HWD_TABLET_TOOL_MODE_ABSOLUTE,
    HWD_TABLET_TOOL_MODE_RELATIVE,
};

struct hwd_tablet_tool {
    struct hwd_seat *seat;
    struct hwd_tablet *tablet;
    struct wlr_tablet_v2_tablet_tool *tablet_v2_tool;

    enum hwd_tablet_tool_mode mode;
    double tilt_x, tilt_y;

    struct wl_listener set_cursor;
    struct wl_listener tool_destroy;
};

struct hwd_tablet_pad {
    struct wl_list link;
    struct hwd_seat_device *seat_device;
    struct hwd_tablet *tablet;
    struct wlr_tablet_v2_tablet_pad *tablet_v2_pad;

    struct wl_listener attach;
    struct wl_listener button;
    struct wl_listener ring;
    struct wl_listener strip;

    struct wlr_surface *current_surface;
    struct wl_listener surface_destroy;

    struct wl_listener tablet_destroy;
};

struct hwd_tablet *
hwd_tablet_create(struct hwd_seat *seat, struct hwd_seat_device *device);

void
hwd_configure_tablet(struct hwd_tablet *tablet);

void
hwd_tablet_destroy(struct hwd_tablet *tablet);

void
hwd_tablet_tool_configure(struct hwd_tablet *tablet, struct wlr_tablet_tool *wlr_tool);

struct hwd_tablet_pad *
hwd_tablet_pad_create(struct hwd_seat *seat, struct hwd_seat_device *device);

void
hwd_configure_tablet_pad(struct hwd_tablet_pad *tablet_pad);

void
hwd_tablet_pad_destroy(struct hwd_tablet_pad *tablet_pad);

void
hwd_tablet_pad_notify_enter(struct hwd_tablet_pad *tablet_pad, struct wlr_surface *surface);

#endif
