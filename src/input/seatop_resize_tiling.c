#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/input/seatop_resize_tiling.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-server-protocol.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>

#include <hayward/commands.h>
#include <hayward/input/cursor.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_default.h>
#include <hayward/tree/column.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

struct seatop_resize_tiling_event {
    struct hwd_window *window;

    // Container, or ancestor of container which will be resized
    // horizontally/vertically.
    // TODO v_container will always be the selected window.  h_container
    // will always be the containing column.
    struct hwd_column *h_container;
    struct hwd_window *v_container;

    // Sibling container(s) that will be resized to accommodate.  h_sib is
    // always a column.  v_sib is always a window.
    struct hwd_column *h_sib;
    struct hwd_window *v_sib;

    enum wlr_edges edge;
    enum wlr_edges edge_x, edge_y;
    double ref_lx, ref_ly;          // cursor's x/y at start of op
    double h_container_orig_width;  // width of the horizontal ancestor at start
    double v_container_orig_height; // height of the vertical ancestor at start
};

static struct hwd_window *
window_get_resize_sibling(struct hwd_window *window, uint32_t edge) {
    if (edge & WLR_EDGE_TOP) {
        return window_get_previous_sibling(window);
    }

    if (edge & WLR_EDGE_BOTTOM) {
        return window_get_next_sibling(window);
    }

    return NULL;
}

static void
handle_button(
    struct hwd_seat *seat, uint32_t time_msec, struct wlr_input_device *device, uint32_t button,
    enum wl_pointer_button_state state
) {
    struct seatop_resize_tiling_event *e = seat->seatop_data;

    if (seat->cursor->pressed_button_count == 0) {
        if (e->h_container) {
            column_set_resizing(e->h_container, false);
            column_set_resizing(e->h_sib, false);
            workspace_set_dirty(e->h_container->workspace);
        }
        if (e->v_container) {
            window_set_resizing(e->v_container, false);
            window_set_resizing(e->v_sib, false);
            column_set_dirty(e->v_container->parent);
        }
        seatop_begin_default(seat);
    }
}

static void
handle_pointer_motion(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_resize_tiling_event *e = seat->seatop_data;
    int amount_x = 0;
    int amount_y = 0;
    int moved_x = seat->cursor->cursor->x - e->ref_lx;
    int moved_y = seat->cursor->cursor->y - e->ref_ly;

    if (e->h_container) {
        if (e->edge & WLR_EDGE_LEFT) {
            amount_x = (e->h_container_orig_width - moved_x) - e->h_container->pending.width;
        } else if (e->edge & WLR_EDGE_RIGHT) {
            amount_x = (e->h_container_orig_width + moved_x) - e->h_container->pending.width;
        }
    }
    if (e->v_container) {
        if (e->edge & WLR_EDGE_TOP) {
            amount_y = (e->v_container_orig_height - moved_y) - e->v_container->pending.height;
        } else if (e->edge & WLR_EDGE_BOTTOM) {
            amount_y = (e->v_container_orig_height + moved_y) - e->v_container->pending.height;
        }
    }

    if (amount_x != 0) {
        window_resize_tiled(e->window, e->edge_x, amount_x);
    }
    if (amount_y != 0) {
        window_resize_tiled(e->window, e->edge_y, amount_y);
    }
}

static void
handle_unref(struct hwd_seat *seat, struct hwd_window *window) {
    struct seatop_resize_tiling_event *e = seat->seatop_data;
    if (e->window == window) {
        seatop_begin_default(seat);
    }
    if (e->h_sib == window->parent || e->v_sib == window) {
        seatop_begin_default(seat);
    }
}

static const struct hwd_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .unref = handle_unref,
};

void
seatop_begin_resize_tiling(struct hwd_seat *seat, struct hwd_window *window, enum wlr_edges edge) {
    seatop_end(seat);

    struct hwd_workspace *workspace = window->workspace;

    struct seatop_resize_tiling_event *e = calloc(1, sizeof(struct seatop_resize_tiling_event));
    if (!e) {
        return;
    }
    e->window = window;
    e->edge = edge;

    e->ref_lx = seat->cursor->cursor->x;
    e->ref_ly = seat->cursor->cursor->y;

    if (edge & WLR_EDGE_LEFT) {
        e->edge_x = WLR_EDGE_LEFT;
        e->h_container = e->window->parent;
        e->h_sib = workspace_get_column_before(workspace, e->h_container);

        if (e->h_sib) {
            column_set_resizing(e->h_container, true);
            column_set_resizing(e->h_sib, true);
            e->h_container_orig_width = e->h_container->pending.width;
        }
    } else if (edge & WLR_EDGE_RIGHT) {
        e->edge_x = WLR_EDGE_RIGHT;
        e->h_container = e->window->parent;
        e->h_sib = workspace_get_column_after(workspace, e->h_container);

        if (e->h_sib) {
            column_set_resizing(e->h_container, true);
            column_set_resizing(e->h_sib, true);
            e->h_container_orig_width = e->h_container->pending.width;
        }
    }
    if (edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) {
        e->edge_y = edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
        e->v_container = e->window;
        e->v_sib = window_get_resize_sibling(e->v_container, e->edge_y);

        if (e->v_container) {
            window_set_resizing(e->v_container, true);
            window_set_resizing(e->v_sib, true);
            e->v_container_orig_height = e->v_container->pending.height;
        }
    }

    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
