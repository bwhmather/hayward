#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/input/seat.h"

#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>

#include <hayward/desktop.h>
#include <hayward/desktop/transaction.h>
#include <hayward/input/cursor.h>
#include <hayward/input/tablet.h>
#include <hayward/tree/window.h>

#include <config.h>

struct seatop_move_floating_event {
    struct hayward_window *window;
    double dx, dy; // cursor offset in window
};

static void
finalize_move(struct hayward_seat *seat) {
    struct seatop_move_floating_event *e = seat->seatop_data;

    // We "move" the window to its own location
    // so it discovers its output again.
    window_floating_move_to(
        e->window, e->window->pending.x, e->window->pending.y
    );
    transaction_commit_dirty();

    seatop_begin_default(seat);
}

static void
handle_button(
    struct hayward_seat *seat, uint32_t time_msec,
    struct wlr_input_device *device, uint32_t button,
    enum wlr_button_state state
) {
    if (seat->cursor->pressed_button_count == 0) {
        finalize_move(seat);
    }
}

static void
handle_tablet_tool_tip(
    struct hayward_seat *seat, struct hayward_tablet_tool *tool,
    uint32_t time_msec, enum wlr_tablet_tool_tip_state state
) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        finalize_move(seat);
    }
}
static void
handle_pointer_motion(struct hayward_seat *seat, uint32_t time_msec) {
    struct seatop_move_floating_event *e = seat->seatop_data;
    struct wlr_cursor *cursor = seat->cursor->cursor;
    desktop_damage_window(e->window);
    window_floating_move_to(e->window, cursor->x - e->dx, cursor->y - e->dy);
    desktop_damage_window(e->window);
    transaction_commit_dirty();
}

static void
handle_unref(struct hayward_seat *seat, struct hayward_window *window) {
    struct seatop_move_floating_event *e = seat->seatop_data;
    if (e->window == window) {
        seatop_begin_default(seat);
    }
}

static const struct hayward_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .unref = handle_unref,
};

void
seatop_begin_move_floating(
    struct hayward_seat *seat, struct hayward_window *window
) {
    seatop_end(seat);

    struct hayward_cursor *cursor = seat->cursor;
    struct seatop_move_floating_event *e =
        calloc(1, sizeof(struct seatop_move_floating_event));
    if (!e) {
        return;
    }
    e->window = window;
    e->dx = cursor->cursor->x - window->pending.x;
    e->dy = cursor->cursor->y - window->pending.y;

    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    window_raise_floating(window);
    transaction_commit_dirty();

    cursor_set_image(cursor, "grab", NULL);
    wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
