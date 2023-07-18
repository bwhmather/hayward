#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/input/seatop_move_floating.h"

#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_default.h>
#include <hayward/input/tablet.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>

struct seatop_move_floating_event {
    struct hwd_window *window;
    double dx, dy; // cursor offset in window
};

static void
finalize_move(struct hwd_seat *seat) {
    struct seatop_move_floating_event *e = seat->seatop_data;

    // The window is already at the right location, but we want to bind it to
    // the correct output.
    struct hwd_window *window = e->window;
    struct hwd_output *output =
        root_find_closest_output(root, window->pending.x, window->pending.y);

    window_floating_move_to(window, output, window->pending.x, window->pending.y);

    seatop_begin_default(seat);
}

static void
handle_button(
    struct hwd_seat *seat, uint32_t time_msec, struct wlr_input_device *device, uint32_t button,
    enum wlr_button_state state
) {
    if (seat->cursor->pressed_button_count == 0) {
        finalize_move(seat);
    }
}

static void
handle_tablet_tool_tip(
    struct hwd_seat *seat, struct hwd_tablet_tool *tool, uint32_t time_msec,
    enum wlr_tablet_tool_tip_state state
) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        finalize_move(seat);
    }
}
static void
handle_pointer_motion(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_move_floating_event *e = seat->seatop_data;
    struct wlr_cursor *cursor = seat->cursor->cursor;

    struct hwd_window *window = e->window;
    struct hwd_output *output = window->pending.output;

    window_floating_move_to(window, output, cursor->x - e->dx, cursor->y - e->dy);
}

static void
handle_unref(struct hwd_seat *seat, struct hwd_window *window) {
    struct seatop_move_floating_event *e = seat->seatop_data;
    if (e->window == window) {
        seatop_begin_default(seat);
    }
}

static const struct hwd_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .unref = handle_unref,
};

void
seatop_begin_move_floating(struct hwd_seat *seat, struct hwd_window *window) {
    seatop_end(seat);

    struct hwd_cursor *cursor = seat->cursor;
    struct seatop_move_floating_event *e = calloc(1, sizeof(struct seatop_move_floating_event));
    if (!e) {
        return;
    }
    e->window = window;
    e->dx = cursor->cursor->x - window->pending.x;
    e->dy = cursor->cursor->y - window->pending.y;

    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    window_raise_floating(window);

    cursor_set_image(cursor, "grab", NULL);
    wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
