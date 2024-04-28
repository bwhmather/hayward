#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/input/seatop_move.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-server-protocol.h>

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
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

struct seatop_move_event {
    struct hwd_window *window;
    double dx, dy; // cursor offset in window

    struct wlr_box target_area;
    struct hwd_column *destination_column;

    double ref_lx, ref_ly; // Cursor's position at start of operation.
    bool threshold_reached;
};

static void
finalize_move(struct hwd_seat *seat) {
    struct seatop_move_event *e = seat->seatop_data;

    if (!e->threshold_reached) {
        seatop_begin_default(seat);
        return;
    }

    if (e->destination_column != NULL) {
        window_detach(e->window);
        if (e->destination_column->pending.preview_target != NULL) {
            column_add_sibling(e->destination_column->pending.preview_target, e->window, true);
        } else {
            column_insert_child(e->destination_column, e->window, 0);
        }
        root_set_focused_window(root, e->window);
        e->destination_column->pending.show_preview = false;
        e->destination_column->pending.preview_target = NULL;
        column_set_dirty(e->destination_column);
    } else {
        // The window is already at the right location, but we want to bind it to
        // the correct output.
        struct hwd_window *window = e->window;
        struct hwd_output *output =
            root_find_closest_output(root, window->pending.x, window->pending.y);

        window_floating_move_to(window, output, window->pending.x, window->pending.y);
    }

    window_set_moving(e->window, false);
    seatop_begin_default(seat);
    root_commit_focus(root);
}

static void
handle_button(
    struct hwd_seat *seat, uint32_t time_msec, struct wlr_input_device *device, uint32_t button,
    enum wl_pointer_button_state state
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
do_detach(struct hwd_seat *seat) {
    struct hwd_cursor *cursor = seat->cursor;
    struct seatop_move_event *e = seat->seatop_data;
    struct hwd_window *window = e->window;

    if (!window_is_floating(window)) {
        struct hwd_workspace *workspace = window->workspace;
        struct hwd_output *output = window_get_output(window);

        bool on_titlebar = e->ref_ly - window->pending.y <= window->pending.titlebar_height;

        double dx = e->ref_lx - window->pending.x;
        double dy = e->ref_ly - window->pending.y;
        double fdx = dx / window->pending.width;
        double fdy = dy / window->pending.height;

        struct hwd_column *old_parent = window->parent;
        window_detach(window);
        workspace_add_floating(workspace, window);
        if (old_parent) {
            column_consider_destroy(old_parent);
        }

        // TODO should use saved size if possible.
        window_floating_set_default_size(window);
        window_floating_move_to(
            window, output, e->ref_lx - fdx * window->pending.width,
            e->ref_ly - (on_titlebar ? dy : (fdy * window->pending.height))
        );

        root_set_focused_window(root, window);
    }

    e->dx = e->ref_lx - window->pending.x;
    e->dy = e->ref_ly - window->pending.y;

    window_raise_floating(window);
    window_set_moving(window, true);
    root_commit_focus(root);

    cursor_set_image(cursor, "grab", NULL);
    wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}

static void
handle_pointer_motion_prethreshold(struct hwd_seat *seat) {
    struct seatop_move_event *e = seat->seatop_data;
    struct wlr_cursor *cursor = seat->cursor->cursor;

    double threshold = config->tiling_drag_threshold;

    double x_offset = cursor->x - e->ref_lx;
    double y_offset = cursor->y - e->ref_lx;

    if ((x_offset * x_offset) + (y_offset * y_offset) > threshold * threshold) {
        e->threshold_reached = true;
        do_detach(seat);
    }
}

static void
handle_pointer_motion_postthreshold(struct hwd_seat *seat) {
    struct seatop_move_event *e = seat->seatop_data;
    struct wlr_cursor *cursor = seat->cursor->cursor;

    struct hwd_window *window = e->window;
    struct hwd_output *output = window_get_output(window);
    struct hwd_workspace *workspace = root_get_active_workspace(root);

    window_floating_move_to(window, output, cursor->x - e->dx, cursor->y - e->dy);

    // === Check if we have left the current snap target ===
    if (e->destination_column != NULL &&
        !wlr_box_contains_point(&e->target_area, cursor->x, cursor->y)) {
        e->destination_column->pending.show_preview = false;
        e->destination_column->pending.preview_target = NULL;
        column_consider_destroy(e->destination_column);
        e->destination_column = NULL;
        workspace_set_dirty(workspace);
    }

    if (e->destination_column != NULL) {
        e->destination_column->preview_anchor_x = cursor->x;
        e->destination_column->preview_anchor_y = cursor->y;
        column_set_dirty(e->destination_column);
        return;
    }

    // === Find new target ===
    // Snapping changes the pending state of the window tree and so that is the
    // state that we need to query.  Using, for example, `seat_get_target_at`
    // would result in us updating the pending state based on old values.  This
    // could result in weird oscilations.  Most other seat operations should use
    // the current state.

    struct hwd_window *floating_window =
        workspace_get_floating_window_at(workspace, cursor->x, cursor->y);
    if (floating_window != NULL) {
        // Output obscured by floating window.  No snapping possible.
        return;
    }

    struct hwd_output *target_output = root_get_output_at(root, cursor->x, cursor->y);
    if (target_output == NULL) {
        // TODO cursor constraints should ensure this.  Could probably be an
        // assertion.
        return;
    }

    // Are we near the edge of the output?
    //   - Create placeholder column and draw preview square over whole thing.
    //   - Exit when moved to different output, or some distance away from edge.
    struct wlr_box left_box;
    output_get_box(target_output, &left_box);
    left_box.width = 5;
    if (wlr_box_contains_point(&left_box, cursor->x, cursor->y)) {
        struct hwd_column *destination_column = column_create();
        destination_column->pending.show_preview = true;
        destination_column->preview_height_fraction = window->height_fraction;
        destination_column->preview_baseline = e->dy / window->pending.height;
        destination_column->preview_anchor_x = cursor->x;
        destination_column->preview_anchor_y = cursor->y;
        workspace_insert_column_first(workspace, target_output, destination_column);

        workspace_set_dirty(workspace);

        e->destination_column = destination_column;
        e->target_area.x = left_box.x;
        e->target_area.y = left_box.y;
        e->target_area.width = 40;
        e->target_area.height = left_box.height;

        return;
    }
    struct wlr_box right_box;
    output_get_box(target_output, &right_box);
    right_box.x += right_box.width - 5;
    right_box.width = 5;
    if (wlr_box_contains_point(&right_box, cursor->x, cursor->y)) {
        struct hwd_column *destination_column = column_create();
        destination_column->pending.show_preview = true;
        destination_column->preview_height_fraction = window->height_fraction;
        destination_column->preview_baseline = e->dy / window->pending.height;
        destination_column->preview_anchor_x = cursor->x;
        destination_column->preview_anchor_y = cursor->y;
        workspace_insert_column_last(workspace, target_output, destination_column);

        workspace_set_dirty(workspace);

        e->destination_column = destination_column;
        e->target_area.x = right_box.x - 35;
        e->target_area.y = right_box.y;
        e->target_area.width = 40;
        e->target_area.height = right_box.height;

        return;
    }

    struct hwd_column *target_column = workspace_get_column_at(workspace, cursor->x, cursor->y);
    if (target_column == NULL) {
        return;
    }

    // Are we near the center of a column?
    //  - Find the insertion point that minimizes distance of the anchor point
    //    from the cursor.
    //  - Exit when moved to the next insertion point or out of the center.
    struct wlr_box centre_box;
    column_get_box(target_column, &centre_box);
    centre_box.width /= 5;
    centre_box.x += 2 * centre_box.width;
    if (wlr_box_contains_point(&centre_box, cursor->x, cursor->y)) {
        e->target_area = centre_box;

        struct hwd_column *destination_column = target_column;
        destination_column->pending.show_preview = true;
        destination_column->preview_height_fraction = window->height_fraction;
        destination_column->preview_baseline = e->dy / window->pending.height;
        destination_column->preview_anchor_x = cursor->x;
        destination_column->preview_anchor_y = cursor->y;

        column_set_dirty(destination_column);

        e->destination_column = destination_column;
        return;
    }

    // Are we over the edge of a column?
    //   - Create placeholder column and draw preview square over whole thing.
    //   - Exit when we moved outside of slightly larger square (probably a bit
    //     smaller than placeholder column).
    if (cursor->x - target_column->pending.x < 20) {
        struct hwd_column *destination_column = column_create();
        destination_column->pending.show_preview = true;
        destination_column->preview_height_fraction = window->height_fraction;
        destination_column->preview_baseline = e->dy / window->pending.height;
        destination_column->preview_anchor_x = cursor->x;
        destination_column->preview_anchor_y = cursor->y;
        workspace_insert_column_before(workspace, target_column, destination_column);

        workspace_set_dirty(workspace);

        e->destination_column = destination_column;
        e->target_area.x = target_column->pending.x - 40;
        e->target_area.y = target_column->pending.y;
        e->target_area.width = 80;
        e->target_area.height = target_column->pending.height;

        return;
    }
    if (target_column->pending.x + target_column->pending.width - cursor->x < 20) {
        struct hwd_column *destination_column = column_create();
        destination_column->pending.show_preview = true;
        destination_column->preview_height_fraction = window->height_fraction;
        destination_column->preview_baseline = e->dy / window->pending.height;
        destination_column->preview_anchor_x = cursor->x;
        destination_column->preview_anchor_y = cursor->y;
        workspace_insert_column_after(workspace, target_column, destination_column);

        workspace_set_dirty(workspace);

        e->destination_column = destination_column;
        e->target_area.x = target_column->pending.x + target_column->pending.width - 40;
        e->target_area.y = target_column->pending.y;
        e->target_area.width = 80; // We want to extend over the next column as well.
        e->target_area.height = target_column->pending.height;

        return;
    }
}

static void
handle_pointer_motion(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_move_event *e = seat->seatop_data;
    if (!e->threshold_reached) {
        handle_pointer_motion_prethreshold(seat);
    } else {
        handle_pointer_motion_postthreshold(seat);
    }
}

static void
handle_end(struct hwd_seat *seat) {
    struct seatop_move_event *e = seat->seatop_data;
    if (e->window != NULL) {
        window_set_moving(e->window, false);
    }
}

static void
handle_unref(struct hwd_seat *seat, struct hwd_window *window) {
    struct seatop_move_event *e = seat->seatop_data;
    // TODO track unref column instead.
    if (e->destination_column) {
        e->destination_column->pending.show_preview = false;
        e->destination_column->pending.preview_target = NULL;
        column_consider_destroy(e->destination_column);
        e->destination_column = NULL;
    }

    if (e->window == window) {
        e->window = NULL;
        seatop_begin_default(seat);
        root_commit_focus(root);
    }
}

static const struct hwd_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .end = handle_end,
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

    e->ref_lx = cursor->cursor->x;
    e->ref_ly = cursor->cursor->y;

    root_set_focused_window(root, e->window);
}
