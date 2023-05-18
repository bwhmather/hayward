#ifndef HAYWARD_INPUT_TABLET_H
#define HAYWARD_INPUT_TABLET_H

#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>

struct hayward_seat;
struct wlr_tablet_tool;

struct hayward_tablet {
    struct wl_list link;
    struct hayward_seat_device *seat_device;
    struct wlr_tablet_v2_tablet *tablet_v2;
};

enum hayward_tablet_tool_mode {
    HAYWARD_TABLET_TOOL_MODE_ABSOLUTE,
    HAYWARD_TABLET_TOOL_MODE_RELATIVE,
};

struct hayward_tablet_tool {
    struct hayward_seat *seat;
    struct hayward_tablet *tablet;
    struct wlr_tablet_v2_tablet_tool *tablet_v2_tool;

    enum hayward_tablet_tool_mode mode;
    double tilt_x, tilt_y;

    struct wl_listener set_cursor;
    struct wl_listener tool_destroy;
};

struct hayward_tablet_pad {
    struct wl_list link;
    struct hayward_seat_device *seat_device;
    struct hayward_tablet *tablet;
    struct wlr_tablet_v2_tablet_pad *tablet_v2_pad;

    struct wl_listener attach;
    struct wl_listener button;
    struct wl_listener ring;
    struct wl_listener strip;

    struct wlr_surface *current_surface;
    struct wl_listener surface_destroy;

    struct wl_listener tablet_destroy;
};

struct hayward_tablet *
hayward_tablet_create(
    struct hayward_seat *seat, struct hayward_seat_device *device
);

void
hayward_configure_tablet(struct hayward_tablet *tablet);

void
hayward_tablet_destroy(struct hayward_tablet *tablet);

void
hayward_tablet_tool_configure(
    struct hayward_tablet *tablet, struct wlr_tablet_tool *wlr_tool
);

struct hayward_tablet_pad *
hayward_tablet_pad_create(
    struct hayward_seat *seat, struct hayward_seat_device *device
);

void
hayward_configure_tablet_pad(struct hayward_tablet_pad *tablet_pad);

void
hayward_tablet_pad_destroy(struct hayward_tablet_pad *tablet_pad);

void
hayward_tablet_pad_notify_enter(
    struct hayward_tablet_pad *tablet_pad, struct wlr_surface *surface
);

#endif
