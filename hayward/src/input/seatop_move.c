#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/input/seatop_move.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/util/box.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_default.h>
#include <hayward/input/tablet.h>
#include <hayward/output.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

struct seatop_move_event {
    struct hwd_window *window;
    double dx, dy; // cursor offset in window

    struct wlr_box target_area;
    struct hwd_column *target_column;
};

static void
finalize_move(struct hwd_seat *seat) {
    struct seatop_move_event *e = seat->seatop_data;

    if (e->target_column != NULL) {
        window_detach(e->window);
        column_add_child(e->target_column, e->window);
        arrange_column(e->target_column);
    } else {
        // The window is already at the right location, but we want to bind it to
        // the correct output.
        struct hwd_window *window = e->window;
        struct hwd_output *output =
            root_find_closest_output(root, window->pending.x, window->pending.y);

        window_floating_move_to(window, output, window->pending.x, window->pending.y);
    }

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
    struct seatop_move_event *e = seat->seatop_data;
    struct wlr_cursor *cursor = seat->cursor->cursor;

    struct hwd_window *window = e->window;
    struct hwd_output *output = window->pending.output;
    struct hwd_workspace *workspace = root_get_active_workspace(root);

    if (e->target_column != NULL &&
        !wlr_box_contains_point(&e->target_area, cursor->x, cursor->y)) {
        column_consider_destroy(e->target_column);
        // TODO clear preview state.
        e->target_column = NULL;
        arrange_workspace(workspace);
    }

    if (e->target_column == NULL) {
        struct hwd_output *target_output = NULL;
        struct hwd_window *target_window = NULL;
        struct wlr_surface *surface = NULL;
        double sx, sy = 0;

        seat_get_target_at(
            seat, cursor->x, cursor->y, &target_output, &target_window, &surface, &sx, &sy
        );

        if (target_output == NULL) {
            return;
        }

        // Are we near the edge of the output?
        //   - Create placeholder column and draw preview square over whole thing.
        //   - Exit when moved to different output, or some distance away from edge.
        // State: output is output, column is placeholder column, behind_window is NULL.
        if (cursor->x - target_output->lx < 20) {
            struct hwd_column *target_column = column_create();
            workspace_insert_tiling(workspace, target_output, target_column, 0);
            arrange_workspace(workspace);

            e->target_area.x = target_output->lx;
            e->target_area.y = target_output->ly;
            e->target_area.width = 40;
            e->target_area.height = target_output->height;

            e->target_column = target_column;
        }

        if (target_output->lx + target_output->width - cursor->x < 20) {
            struct hwd_column *target_column = column_create();
            workspace_insert_tiling(workspace, target_output, target_column, 0);
            arrange_workspace(workspace);

            e->target_area.x = target_output->lx + target_output->width - 40;
            e->target_area.y = target_output->ly;
            e->target_area.width = 40;
            e->target_area.height = target_output->height;

            e->target_column = target_column;
        }

        // Are we over the edge of a column?
        //   - Create placeholder column and draw preview square over whole thing.
        //   - Exit when we moved outside of slightly larger square (probably a bit
        //     smaller than placeholder column).
        // State: output is output, column is placeholder column, behind window is NULL.
        // TODO

        // Are we at the center of a tiled window?
        //   - Draw preview square on top of the active window.
        //   - Exit when moved outside of larger square.
        // TODO

        // Are we over the titlebar of a tiled window?
        //   - Move titlebar towards active window and draw preview square in its place.
        //   - Exit when moved outside the original titlebar square.  No hysterisis.
        // TODO
    }

    window_floating_move_to(window, output, cursor->x - e->dx, cursor->y - e->dy);
}

static void
handle_unref(struct hwd_seat *seat, struct hwd_window *window) {
    struct seatop_move_event *e = seat->seatop_data;
    // TODO track unref column instead.
    e->target_column = NULL;
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
seatop_begin_move(struct hwd_seat *seat, struct hwd_window *window) {
    seatop_end(seat);

    struct hwd_cursor *cursor = seat->cursor;
    struct seatop_move_event *e = calloc(1, sizeof(struct seatop_move_event));
    if (!e) {
        return;
    }
    e->window = window;
    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    if (!window_is_floating(window)) {
        struct hwd_workspace *workspace = window->pending.workspace;
        struct hwd_output *output = window->pending.output;

        bool on_titlebar = cursor->cursor->y - window->pending.y <= window_titlebar_height();

        double dx = cursor->cursor->x - window->pending.x;
        double dy = cursor->cursor->y - window->pending.y;
        double fdx = dx / window->pending.width;
        double fdy = dy / window->pending.height;

        // TODO Adjust column widths so that window current column fraction is 1.0.

        struct hwd_column *old_parent = window->pending.parent;
        window_detach(window);
        workspace_add_floating(workspace, window);
        if (old_parent) {
            column_consider_destroy(old_parent);
        }

        // TODO should use saved size if possible.
        window_floating_set_default_size(window);
        window_floating_move_to(
            window, output, cursor->cursor->x - fdx * window->pending.width,
            cursor->cursor->y - (on_titlebar ? dy : (fdy * window->pending.height))
        );

        root_set_focused_window(root, window);
    }

    e->dx = cursor->cursor->x - window->pending.x;
    e->dy = cursor->cursor->y - window->pending.y;

    window_raise_floating(window);

    cursor_set_image(cursor, "grab", NULL);
    wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
