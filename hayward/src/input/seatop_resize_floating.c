#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/input/seatop_resize_floating.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include <wlr/xcursor.h>

#include <hayward/config.h>
#include <hayward/input/cursor.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_default.h>
#include <hayward/input/seatop_resize_floating.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#include <config.h>

struct seatop_resize_floating_event {
    struct hwd_window *window;
    enum wlr_edges edge;
    bool preserve_ratio;
    double ref_lx, ref_ly;               // cursor's x/y at start of op
    double ref_width, ref_height;        // window's size at start of op
    double ref_window_lx, ref_window_ly; // window's x/y at start of op
};

static void
handle_button(
    struct hwd_seat *seat, uint32_t time_msec, struct wlr_input_device *device, uint32_t button,
    enum wlr_button_state state
) {
    struct seatop_resize_floating_event *e = seat->seatop_data;
    struct hwd_window *window = e->window;

    if (seat->cursor->pressed_button_count == 0) {
        window_set_resizing(window, false);
        arrange_window(window); // Send configure w/o resizing hint
        seatop_begin_default(seat);
    }
}

static void
handle_pointer_motion(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_resize_floating_event *e = seat->seatop_data;
    struct hwd_window *window = e->window;
    enum wlr_edges edge = e->edge;
    struct hwd_cursor *cursor = seat->cursor;

    // The amount the mouse has moved since the start of the resize operation
    // Positive is down/right
    double mouse_move_x = cursor->cursor->x - e->ref_lx;
    double mouse_move_y = cursor->cursor->y - e->ref_ly;

    if (edge == WLR_EDGE_TOP || edge == WLR_EDGE_BOTTOM) {
        mouse_move_x = 0;
    }
    if (edge == WLR_EDGE_LEFT || edge == WLR_EDGE_RIGHT) {
        mouse_move_y = 0;
    }

    double grow_width = edge & WLR_EDGE_LEFT ? -mouse_move_x : mouse_move_x;
    double grow_height = edge & WLR_EDGE_TOP ? -mouse_move_y : mouse_move_y;

    if (e->preserve_ratio) {
        double x_multiplier = grow_width / e->ref_width;
        double y_multiplier = grow_height / e->ref_height;
        double max_multiplier = fmax(x_multiplier, y_multiplier);
        grow_width = e->ref_width * max_multiplier;
        grow_height = e->ref_height * max_multiplier;
    }

    struct hwd_window_state *state = &window->current;
    double border_width = 0.0;
    if (window->current.border == B_NORMAL || window->current.border == B_PIXEL) {
        border_width = state->border_thickness * 2;
    }
    double border_height = 0.0;
    if (window->current.border == B_NORMAL) {
        border_height += window_titlebar_height();
        border_height += state->border_thickness;
    } else if (window->current.border == B_PIXEL) {
        border_height += state->border_thickness * 2;
    }

    // Determine new width/height, and accommodate for floating min/max values
    double width = e->ref_width + grow_width;
    double height = e->ref_height + grow_height;
    int min_width, max_width, min_height, max_height;
    floating_calculate_constraints(&min_width, &max_width, &min_height, &max_height);
    width = fmin(width, max_width - border_width);
    width = fmax(width, min_width + border_width);
    width = fmax(width, 1);
    height = fmin(height, max_height - border_height);
    height = fmax(height, min_height + border_height);
    height = fmax(height, 1);

    // Apply the view's min/max size
    if (window->view) {
        double view_min_width, view_max_width, view_min_height, view_max_height;
        view_get_constraints(
            window->view, &view_min_width, &view_max_width, &view_min_height, &view_max_height
        );
        width = fmin(width, view_max_width - border_width);
        width = fmax(width, view_min_width + border_width);
        width = fmax(width, 1);
        height = fmin(height, view_max_height - border_height);
        height = fmax(height, view_min_height + border_height);
        height = fmax(height, 1);
    }

    // Recalculate these, in case we hit a min/max limit
    grow_width = width - e->ref_width;
    grow_height = height - e->ref_height;

    // Determine grow x/y values - these are relative to the window's x/y at
    // the start of the resize operation.
    double grow_x = 0, grow_y = 0;
    if (edge & WLR_EDGE_LEFT) {
        grow_x = -grow_width;
    } else if (edge & WLR_EDGE_RIGHT) {
        grow_x = 0;
    } else {
        grow_x = -grow_width / 2;
    }
    if (edge & WLR_EDGE_TOP) {
        grow_y = -grow_height;
    } else if (edge & WLR_EDGE_BOTTOM) {
        grow_y = 0;
    } else {
        grow_y = -grow_height / 2;
    }

    // Determine the amounts we need to bump everything relative to the current
    // size.
    int relative_grow_width = width - window->pending.width;
    int relative_grow_height = height - window->pending.height;
    int relative_grow_x = (e->ref_window_lx + grow_x) - window->pending.x;
    int relative_grow_y = (e->ref_window_ly + grow_y) - window->pending.y;

    // Actually resize stuff
    window->pending.x += relative_grow_x;
    window->pending.y += relative_grow_y;
    window->pending.width += relative_grow_width;
    window->pending.height += relative_grow_height;

    window->pending.content_x += relative_grow_x;
    window->pending.content_y += relative_grow_y;
    window->pending.content_width += relative_grow_width;
    window->pending.content_height += relative_grow_height;

    arrange_window(window);
}

static void
handle_unref(struct hwd_seat *seat, struct hwd_window *window) {
    struct seatop_resize_floating_event *e = seat->seatop_data;
    if (e->window == window) {
        seatop_begin_default(seat);
    }
}

static const struct hwd_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .unref = handle_unref,
};

void
seatop_begin_resize_floating(
    struct hwd_seat *seat, struct hwd_window *window, enum wlr_edges edge
) {
    seatop_end(seat);

    struct seatop_resize_floating_event *e = calloc(1, sizeof(struct seatop_resize_floating_event));
    if (!e) {
        return;
    }
    e->window = window;

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
    e->preserve_ratio = keyboard && (wlr_keyboard_get_modifiers(keyboard) & WLR_MODIFIER_SHIFT);

    e->edge = edge == WLR_EDGE_NONE ? WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT : edge;
    e->ref_lx = seat->cursor->cursor->x;
    e->ref_ly = seat->cursor->cursor->y;
    e->ref_window_lx = window->pending.x;
    e->ref_window_ly = window->pending.y;
    e->ref_width = window->pending.width;
    e->ref_height = window->pending.height;

    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    window_set_resizing(window, true);
    window_raise_floating(window);

    const char *image = edge == WLR_EDGE_NONE ? "se-resize" : wlr_xcursor_get_resize_name(edge);
    cursor_set_image(seat->cursor, image, NULL);
    wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
