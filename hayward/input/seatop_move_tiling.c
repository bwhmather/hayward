#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/util/edges.h>
#include "hayward/desktop.h"
#include "hayward/desktop/transaction.h"
#include "hayward/input/cursor.h"
#include "hayward/input/seat.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/node.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "hayward/tree/column.h"
#include "hayward/tree/window.h"
#include "log.h"
#include "util.h"

// Thickness of the dropzone when dragging to the edge of a layout container
#define DROP_LAYOUT_BORDER 30

// Thickness of indicator when dropping onto a titlebar.  This should be a
// multiple of 2.
#define DROP_SPLIT_INDICATOR 10

struct seatop_move_tiling_event {
	struct hayward_window *moving_window;
	struct hayward_output *target_output;
	struct hayward_window *target_window;
	enum wlr_edges target_edge;
	struct wlr_box drop_box;
	double ref_lx, ref_ly; // cursor's x/y at start of op
	bool threshold_reached;
	bool insert_after_target;
};

static void handle_render(struct hayward_seat *seat,
		struct hayward_output *output, pixman_region32_t *damage) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (!e->threshold_reached) {
		return;
	}

	if (!e->target_output) {
	       return;
	}

	float color[4];
	memcpy(&color, config->border_colors.focused.indicator,
			sizeof(float) * 4);
	premultiply_alpha(color, 0.5);
	struct wlr_box box;
	memcpy(&box, &e->drop_box, sizeof(struct wlr_box));
	scale_box(&box, output->wlr_output->scale);
	render_rect(output, damage, &box, color);
}

static void handle_motion_prethreshold(struct hayward_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	double cx = seat->cursor->cursor->x;
	double cy = seat->cursor->cursor->y;
	double sx = e->ref_lx;
	double sy = e->ref_ly;

	// Get the scaled threshold for the output. Even if the operation goes
	// across multiple outputs of varying scales, just use the scale for the
	// output that the cursor is currently on for simplicity.
	struct wlr_output *wlr_output = wlr_output_layout_output_at(
			root->output_layout, cx, cy);
	double output_scale = wlr_output ? wlr_output->scale : 1;
	double threshold = config->tiling_drag_threshold * output_scale;
	threshold *= threshold;

	// If the threshold has been exceeded, start the actual drag
	if ((cx - sx) * (cx - sx) + (cy - sy) * (cy - sy) > threshold) {
		e->threshold_reached = true;
		cursor_set_image(seat->cursor, "grab", NULL);
	}
}

static void resize_box(struct wlr_box *box, enum wlr_edges edge,
		int thickness) {
	switch (edge) {
	case WLR_EDGE_TOP:
		box->height = thickness;
		break;
	case WLR_EDGE_LEFT:
		box->width = thickness;
		break;
	case WLR_EDGE_RIGHT:
		box->x = box->x + box->width - thickness;
		box->width = thickness;
		break;
	case WLR_EDGE_BOTTOM:
		box->y = box->y + box->height - thickness;
		box->height = thickness;
		break;
	case WLR_EDGE_NONE:
		box->x += thickness;
		box->y += thickness;
		box->width -= thickness * 2;
		box->height -= thickness * 2;
		break;
	}
}

static void handle_motion_postthreshold(struct hayward_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	struct hayward_output *target_output = NULL;
	struct hayward_window *target_window = NULL;
	struct wlr_surface *surface = NULL;
	double sx, sy;
	struct hayward_cursor *cursor = seat->cursor;

	seat_get_target_at(
		seat, cursor->cursor->x, cursor->cursor->y,
		&target_output, &target_window,
		&surface, &sx, &sy
	);

	// Damage the old location
	desktop_damage_box(&e->drop_box);

	if (!target_output && !target_window) {
		// Eg. hovered over a layer surface such as haywardbar
		e->target_output = NULL;
		e->target_window = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	if (target_output && !target_window) {
		// Empty output
		e->target_output = target_output;
		e->target_window = target_window;
		e->target_edge = WLR_EDGE_NONE;
		output_get_usable_area(target_output, &e->drop_box);
		desktop_damage_box(&e->drop_box);
		return;
	}

	// Deny moving within own workspace if this is the only child
	if (workspace_num_tiling_views(e->moving_window->pending.workspace) == 1 &&
			target_window->pending.workspace == e->moving_window->pending.workspace) {
		e->target_output = NULL;
		e->target_window = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	hayward_assert(target_output && target_window, "Mouse over unowned window");

	// Moving window to itself should be a no-op.
	if (e->target_window == e->moving_window) {
		e->target_output = NULL;
		e->target_window = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	// Use the hovered view - but we must be over the actual surface
	if (!target_window->view->surface) {
		e->target_output = NULL;
		e->target_window = NULL;
		e->target_edge = WLR_EDGE_NONE;
		return;
	}

	// Find the closest edge
	size_t thickness = fmin(target_window->pending.content_width, target_window->pending.content_height) * 0.3;
	size_t closest_dist = INT_MAX;
	size_t dist;
	e->target_edge = WLR_EDGE_NONE;
	if ((dist = cursor->cursor->y - target_window->pending.y) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_TOP;
	}
	if ((dist = cursor->cursor->x - target_window->pending.x) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_LEFT;
	}
	if ((dist = target_window->pending.x + target_window->pending.width - cursor->cursor->x) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_RIGHT;
	}
	if ((dist = target_window->pending.y + target_window->pending.height - cursor->cursor->y) < closest_dist) {
		closest_dist = dist;
		e->target_edge = WLR_EDGE_BOTTOM;
	}

	if (closest_dist > thickness) {
		e->target_edge = WLR_EDGE_NONE;
	}

	e->target_output = target_output;
	e->target_window = target_window;
	e->drop_box.x = target_window->pending.content_x;
	e->drop_box.y = target_window->pending.content_y;
	e->drop_box.width = target_window->pending.content_width;
	e->drop_box.height = target_window->pending.content_height;
	resize_box(&e->drop_box, e->target_edge, thickness);
	desktop_damage_box(&e->drop_box);
}

static void handle_pointer_motion(struct hayward_seat *seat, uint32_t time_msec) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e->threshold_reached) {
		handle_motion_postthreshold(seat);
	} else {
		handle_motion_prethreshold(seat);
	}
	transaction_commit_dirty();
}

static void finalize_move(struct hayward_seat *seat) {
	struct seatop_move_tiling_event *e = seat->seatop_data;

	struct hayward_window *moving_window = e->moving_window;
	struct hayward_column *old_parent = moving_window->pending.parent;
	struct hayward_workspace *old_workspace = moving_window->pending.workspace;

	struct hayward_workspace *target_workspace = root_get_active_workspace();
	struct hayward_output *target_output = e->target_output;
	struct hayward_window *target_window = e->target_window;
	enum wlr_edges target_edge = e->target_edge;

	// No move target.  Leave window where it is.
	if (target_workspace == NULL) {
		seatop_begin_default(seat);
		return;
	}

	// Move container into empty workspace.
	if (target_window == NULL) {
		window_move_to_workspace(moving_window, target_workspace);
	} else {
		if (target_edge == WLR_EDGE_LEFT || target_edge == WLR_EDGE_RIGHT) {
			struct hayward_column *target_column = target_window->pending.parent;
			int target_column_index = list_find(target_workspace->pending.tiling, target_column);

			struct hayward_column *new_column = column_create();
			new_column->pending.height = new_column->pending.width = 0;
			new_column->width_fraction = 0;
			new_column->pending.layout = L_STACKED;

			int new_column_index = target_edge == WLR_EDGE_LEFT ? target_column_index : target_column_index + 1;

			workspace_insert_tiling(target_workspace, target_output, new_column, new_column_index);

			window_move_to_column(moving_window, new_column);
		} else {
			// TODO (hayward) fix different level of abstraction.
			window_detach(moving_window);
			column_add_sibling(target_window, moving_window, target_edge != WLR_EDGE_TOP);
			ipc_event_window(moving_window, "move");
		}
	}

	if (old_parent) {
		column_consider_destroy(old_parent);
	}

	// This is a bit dirty, but we'll set the dimensions to that of a sibling.
	// I don't think there's any other way to make it consistent without
	// changing how we auto-size containers.
	list_t *siblings = window_get_siblings(moving_window);
	if (siblings->length > 1) {
		int index = list_find(siblings, moving_window);
		struct hayward_window *sibling = index == 0 ?
			siblings->items[1] : siblings->items[index - 1];
		moving_window->pending.width = sibling->pending.width;
		moving_window->pending.height = sibling->pending.height;
		moving_window->width_fraction = sibling->width_fraction;
		moving_window->height_fraction = sibling->height_fraction;
	}

	arrange_workspace(old_workspace);
	if (target_workspace != old_workspace) {
		arrange_workspace(target_workspace);
	}

	transaction_commit_dirty();
	seatop_begin_default(seat);
}

static void handle_button(struct hayward_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	if (seat->cursor->pressed_button_count == 0) {
		finalize_move(seat);
	}
}

static void handle_tablet_tool_tip(struct hayward_seat *seat,
		struct hayward_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (state == WLR_TABLET_TOOL_TIP_UP) {
		finalize_move(seat);
	}
}

static void handle_unref(struct hayward_seat *seat, struct hayward_window *container) {
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e->target_window == container) {
		e->target_output = NULL;
		e->target_window = NULL;
	}
	if (e->moving_window == container) {
		seatop_begin_default(seat);
	}
}

static const struct hayward_seatop_impl seatop_impl = {
	.button = handle_button,
	.pointer_motion = handle_pointer_motion,
	.tablet_tool_tip = handle_tablet_tool_tip,
	.unref = handle_unref,
	.render = handle_render,
};

void seatop_begin_move_tiling_threshold(struct hayward_seat *seat,
		struct hayward_window *moving_window) {
	seatop_end(seat);

	struct seatop_move_tiling_event *e =
		calloc(1, sizeof(struct seatop_move_tiling_event));
	if (!e) {
		return;
	}
	e->moving_window = moving_window;
	e->ref_lx = seat->cursor->cursor->x;
	e->ref_ly = seat->cursor->cursor->y;

	seat->seatop_impl = &seatop_impl;
	seat->seatop_data = e;

	window_raise_floating(moving_window);
	transaction_commit_dirty();
	wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
}

void seatop_begin_move_tiling(struct hayward_seat *seat,
		struct hayward_window *container) {
	seatop_begin_move_tiling_threshold(seat, container);
	struct seatop_move_tiling_event *e = seat->seatop_data;
	if (e) {
		e->threshold_reached = true;
		cursor_set_image(seat->cursor, "grab", NULL);
	}
}
