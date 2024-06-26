#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/input/seatop_down.h"

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>

#include <hayward/config.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_default.h>
#include <hayward/input/tablet.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

struct seatop_down_event {
    struct hwd_window *window;
    struct hwd_seat *seat;
    struct wl_listener surface_destroy;
    struct wlr_surface *surface;
    double ref_lx, ref_ly;               // cursor's x/y at start of op
    double ref_window_lx, ref_window_ly; // window's x/y at start of op
};

static void
handle_pointer_axis(struct hwd_seat *seat, struct wlr_pointer_axis_event *event) {
    struct hwd_input_device *input_device = event->pointer ? event->pointer->base.data : NULL;
    struct input_config *ic = input_device ? input_device_get_config(input_device) : NULL;
    float scroll_factor = (ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

    wlr_seat_pointer_notify_axis(
        seat->wlr_seat, event->time_msec, event->orientation, scroll_factor * event->delta,
        round(scroll_factor * event->delta_discrete), event->source, event->relative_direction
    );
}

static void
handle_button(
    struct hwd_seat *seat, uint32_t time_msec, struct wlr_input_device *device, uint32_t button,
    enum wl_pointer_button_state state
) {
    seat_pointer_notify_button(seat, time_msec, button, state);

    if (seat->cursor->pressed_button_count == 0) {
        seatop_begin_default(seat);
    }
}

static void
handle_pointer_motion(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_down_event *e = seat->seatop_data;
    if (seat_is_input_allowed(seat, e->surface)) {
        double moved_x = seat->cursor->cursor->x - e->ref_lx;
        double moved_y = seat->cursor->cursor->y - e->ref_ly;
        double sx = e->ref_window_lx + moved_x;
        double sy = e->ref_window_ly + moved_y;
        wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
    }
}

static void
handle_tablet_tool_tip(
    struct hwd_seat *seat, struct hwd_tablet_tool *tool, uint32_t time_msec,
    enum wlr_tablet_tool_tip_state state
) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
        seatop_begin_default(seat);
    }
}

static void
handle_tablet_tool_motion(struct hwd_seat *seat, struct hwd_tablet_tool *tool, uint32_t time_msec) {
    struct seatop_down_event *e = seat->seatop_data;
    if (seat_is_input_allowed(seat, e->surface)) {
        double moved_x = seat->cursor->cursor->x - e->ref_lx;
        double moved_y = seat->cursor->cursor->y - e->ref_ly;
        double sx = e->ref_window_lx + moved_x;
        double sy = e->ref_window_ly + moved_y;
        wlr_tablet_v2_tablet_tool_notify_motion(tool->tablet_v2_tool, sx, sy);
    }
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct seatop_down_event *e = wl_container_of(listener, e, surface_destroy);
    if (e) {
        seatop_begin_default(e->seat);
    }
}

static void
handle_unref(struct hwd_seat *seat, struct hwd_window *window) {
    struct seatop_down_event *e = seat->seatop_data;
    if (e->window == window) {
        seatop_begin_default(seat);
    }
}

static void
handle_end(struct hwd_seat *seat) {
    struct seatop_down_event *e = seat->seatop_data;
    wl_list_remove(&e->surface_destroy.link);
}

static const struct hwd_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .pointer_axis = handle_pointer_axis,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .tablet_tool_motion = handle_tablet_tool_motion,
    .unref = handle_unref,
    .end = handle_end,
    .allow_set_cursor = true,
};

void
seatop_begin_down(
    struct hwd_seat *seat, struct hwd_window *window, uint32_t time_msec, double sx, double sy
) {
    seatop_begin_down_on_surface(seat, window->view->surface, time_msec, sx, sy);
    struct seatop_down_event *e = seat->seatop_data;
    e->window = window;

    window_raise_floating(window);
}

void
seatop_begin_down_on_surface(
    struct hwd_seat *seat, struct wlr_surface *surface, uint32_t time_msec, double sx, double sy
) {
    seatop_end(seat);

    struct seatop_down_event *e = calloc(1, sizeof(struct seatop_down_event));
    if (!e) {
        return;
    }
    e->window = NULL;
    e->seat = seat;
    e->surface = surface;
    wl_signal_add(&e->surface->events.destroy, &e->surface_destroy);
    e->surface_destroy.notify = handle_destroy;
    e->ref_lx = seat->cursor->cursor->x;
    e->ref_ly = seat->cursor->cursor->y;
    e->ref_window_lx = sx;
    e->ref_window_ly = sy;

    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;
}
