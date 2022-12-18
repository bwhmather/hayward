#define _POSIX_C_SOURCE 200809L
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>

#include "hayward-common/log.h"

#include "hayward/commands.h"
#include "hayward/desktop/transaction.h"
#include "hayward/input/cursor.h"
#include "hayward/input/seat.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/column.h"
#include "hayward/tree/view.h"
#include "hayward/tree/window.h"

struct seatop_resize_tiling_event {
	struct hayward_window *container; // leaf container

	// Container, or ancestor of container which will be resized
	// horizontally/vertically.
	// TODO v_container will always be the selected window.  h_container
	// will always be the containing column.
	struct hayward_column *h_container;
	struct hayward_window *v_container;

	// Sibling container(s) that will be resized to accommodate.  h_sib is
	// always a column.  v_sib is always a window.
	struct hayward_column *h_sib;
	struct hayward_window *v_sib;

	enum wlr_edges edge;
	enum wlr_edges edge_x, edge_y;
	double ref_lx, ref_ly;			// cursor's x/y at start of op
	double h_container_orig_width;	// width of the horizontal ancestor at start
	double v_container_orig_height; // height of the vertical ancestor at start
};

static struct hayward_column *
column_get_resize_sibling(struct hayward_column *column, uint32_t edge) {
	list_t *siblings = column_get_siblings(column);
	int offset = (edge & WLR_EDGE_LEFT) ? -1 : 1;
	int index = column_sibling_index(column) + offset;

	if (index < 0) {
		return NULL;
	}

	if (index >= siblings->length) {
		return NULL;
	}

	return siblings->items[index];
}

static struct hayward_window *
window_get_resize_sibling(struct hayward_window *window, uint32_t edge) {
	list_t *siblings = window_get_siblings(window);
	int offset = (edge & WLR_EDGE_TOP) ? -1 : 1;
	int index = window_sibling_index(window) + offset;

	if (index < 0) {
		return NULL;
	}

	if (index >= siblings->length) {
		return NULL;
	}

	return siblings->items[index];
}

static void handle_button(
	struct hayward_seat *seat, uint32_t time_msec,
	struct wlr_input_device *device, uint32_t button,
	enum wlr_button_state state
) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;

	if (seat->cursor->pressed_button_count == 0) {
		if (e->h_container) {
			column_set_resizing(e->h_container, false);
			column_set_resizing(e->h_sib, false);
			arrange_workspace(e->h_container->pending.workspace);
		}
		if (e->v_container) {
			window_set_resizing(e->v_container, false);
			window_set_resizing(e->v_sib, false);
			arrange_column(e->v_container->pending.parent);
		}
		transaction_commit_dirty();
		seatop_begin_default(seat);
	}
}

static void
handle_pointer_motion(struct hayward_seat *seat, uint32_t time_msec) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;
	int amount_x = 0;
	int amount_y = 0;
	int moved_x = seat->cursor->cursor->x - e->ref_lx;
	int moved_y = seat->cursor->cursor->y - e->ref_ly;

	if (e->h_container) {
		if (e->edge & WLR_EDGE_LEFT) {
			amount_x = (e->h_container_orig_width - moved_x) -
				e->h_container->pending.width;
		} else if (e->edge & WLR_EDGE_RIGHT) {
			amount_x = (e->h_container_orig_width + moved_x) -
				e->h_container->pending.width;
		}
	}
	if (e->v_container) {
		if (e->edge & WLR_EDGE_TOP) {
			amount_y = (e->v_container_orig_height - moved_y) -
				e->v_container->pending.height;
		} else if (e->edge & WLR_EDGE_BOTTOM) {
			amount_y = (e->v_container_orig_height + moved_y) -
				e->v_container->pending.height;
		}
	}

	if (amount_x != 0) {
		window_resize_tiled(e->container, e->edge_x, amount_x);
	}
	if (amount_y != 0) {
		window_resize_tiled(e->container, e->edge_y, amount_y);
	}
	transaction_commit_dirty();
}

static void
handle_unref(struct hayward_seat *seat, struct hayward_window *container) {
	struct seatop_resize_tiling_event *e = seat->seatop_data;
	if (e->container == container) {
		seatop_begin_default(seat);
	}
	if (e->h_sib == container->pending.parent || e->v_sib == container) {
		seatop_begin_default(seat);
	}
}

static const struct hayward_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.unref = handle_unref,
};

void seatop_begin_resize_tiling(
	struct hayward_seat *seat, struct hayward_window *container,
	enum wlr_edges edge
) {
	seatop_end(seat);

	struct seatop_resize_tiling_event *e =
		calloc(1, sizeof(struct seatop_resize_tiling_event));
	if (!e) {
		return;
	}
	e->container = container;
	e->edge = edge;

	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;

	if (edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
		e->edge_x = edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
		e->h_container = e->container->pending.parent;
		e->h_sib = column_get_resize_sibling(e->h_container, e->edge_x);

		if (e->h_container) {
			column_set_resizing(e->h_container, true);
			column_set_resizing(e->h_sib, true);
			e->h_container_orig_width = e->h_container->pending.width;
		}
	}
	if (edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) {
		e->edge_y = edge & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM);
		e->v_container = e->container;
		e->v_sib = window_get_resize_sibling(e->v_container, e->edge_y);

		if (e->v_container) {
			window_set_resizing(e->v_container, true);
			window_set_resizing(e->v_sib, true);
			e->v_container_orig_height = e->v_container->pending.height;
		}
	}

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	transaction_commit_dirty();
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}
