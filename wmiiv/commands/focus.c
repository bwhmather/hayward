#include <float.h>
#include <strings.h>
#include <wlr/types/wlr_output_layout.h>
#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/cursor.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/root.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "stringop.h"
#include "util.h"

static bool get_direction_from_next_prev(struct wmiiv_container *container,
		struct wmiiv_seat *seat, const char *name, enum wlr_direction *out) {
	enum wmiiv_container_layout parent_layout = L_NONE;
	if (container) {
		parent_layout = container_parent_layout(container);
	}

	if (strcasecmp(name, "prev") == 0) {
		switch (parent_layout) {
		case L_HORIZ:
		case L_TABBED:
			*out = WLR_DIRECTION_LEFT;
			break;
		case L_VERT:
		case L_STACKED:
			*out = WLR_DIRECTION_UP;
			break;
		case L_NONE:
			return true;
		default:
			return false;
		}
	} else if (strcasecmp(name, "next") == 0) {
		switch (parent_layout) {
		case L_HORIZ:
		case L_TABBED:
			*out = WLR_DIRECTION_RIGHT;
			break;
		case L_VERT:
		case L_STACKED:
			*out = WLR_DIRECTION_DOWN;
			break;
		case L_NONE:
			return true;
		default:
			return false;
		}
	} else {
		return false;
	}

	return true;
}

static bool parse_direction(const char *name,
		enum wlr_direction *out) {
	if (strcasecmp(name, "left") == 0) {
		*out = WLR_DIRECTION_LEFT;
	} else if (strcasecmp(name, "right") == 0) {
		*out = WLR_DIRECTION_RIGHT;
	} else if (strcasecmp(name, "up") == 0) {
		*out = WLR_DIRECTION_UP;
	} else if (strcasecmp(name, "down") == 0) {
		*out = WLR_DIRECTION_DOWN;
	} else {
		return false;
	}

	return true;
}

/**
 * Returns the node that should be focused if entering an output by moving
 * in the given direction.
 *
 *  Node should always be either a workspace or a window.
 */
static struct wmiiv_node *get_node_in_output_direction(
		struct wmiiv_output *output, enum wlr_direction dir) {
	struct wmiiv_seat *seat = config->handler_context.seat;
	struct wmiiv_workspace *ws = output_get_active_workspace(output);
	if (!wmiiv_assert(ws, "Expected output to have a workspace")) {
		return NULL;
	}
	if (ws->fullscreen) {
		return &ws->fullscreen->node;
	}
	struct wmiiv_container *col = NULL;
	struct wmiiv_container *win = NULL;

	if (ws->tiling->length > 0) {
		switch (dir) {
		case WLR_DIRECTION_LEFT:
			// get most right child of new output
			col = ws->tiling->items[ws->tiling->length-1];
			win = seat_get_focus_inactive_view(seat, &col->node);
			break;
		case WLR_DIRECTION_RIGHT:
			// get most left child of new output
			col = ws->tiling->items[0];
			win = seat_get_focus_inactive_view(seat, &col->node);
			break;
		case WLR_DIRECTION_UP:
			win = seat_get_focus_inactive_tiling(seat, ws);
			break;
		case WLR_DIRECTION_DOWN:
			win = seat_get_focus_inactive_tiling(seat, ws);
			break;
		}
	}

	if (win) {
		// TODO (wmiir) remove this check once hierarchy is fixed.
		if (container_is_column(win)) {
			win = seat_get_focus_inactive_view(seat, &win->node);
		}
		return &win->node;
	}

	return &ws->node;
}

static struct wmiiv_node *node_get_in_direction_tiling(
		struct wmiiv_container *container, struct wmiiv_seat *seat,
		enum wlr_direction dir) {
	struct wmiiv_container *wrap_candidate = NULL;
	struct wmiiv_container *current = container;
	while (current) {
		if (current->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
			// Fullscreen container with a direction - go straight to outputs
			struct wmiiv_output *output = current->pending.workspace->output;
			struct wmiiv_output *new_output =
				output_get_in_direction(output, dir);
			if (!new_output) {
				return NULL;
			}
			return get_node_in_output_direction(new_output, dir);
		}
		if (current->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
			return NULL;
		}

		bool can_move = false;
		int desired;
		int idx = container_sibling_index(current);
		enum wmiiv_container_layout parent_layout =
			container_parent_layout(current);
		list_t *siblings = container_get_siblings(current);

		if (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT) {
			if (parent_layout == L_HORIZ || parent_layout == L_TABBED) {
				can_move = true;
				desired = idx + (dir == WLR_DIRECTION_LEFT ? -1 : 1);
			}
		} else {
			if (parent_layout == L_VERT || parent_layout == L_STACKED) {
				can_move = true;
				desired = idx + (dir == WLR_DIRECTION_UP ? -1 : 1);
			}
		}

		if (can_move) {
			if (desired < 0 || desired >= siblings->length) {
				int len = siblings->length;
				if (config->focus_wrapping != WRAP_NO && !wrap_candidate
						&& len > 1) {
					if (desired < 0) {
						wrap_candidate = siblings->items[len-1];
					} else {
						wrap_candidate = siblings->items[0];
					}
					if (config->focus_wrapping == WRAP_FORCE) {
						struct wmiiv_container *c = seat_get_focus_inactive_view(
								seat, &wrap_candidate->node);
						return &c->node;
					}
				}
			} else {
				struct wmiiv_container *desired_con = siblings->items[desired];
				struct wmiiv_container *c = seat_get_focus_inactive_view(
						seat, &desired_con->node);
				return &c->node;
			}
		}

		current = current->pending.parent;
	}

	// Check a different output
	struct wmiiv_output *output = container->pending.workspace->output;
	struct wmiiv_output *new_output = output_get_in_direction(output, dir);
	if ((config->focus_wrapping != WRAP_WORKSPACE ||
				container->node.type == N_WORKSPACE) && new_output) {
		return get_node_in_output_direction(new_output, dir);
	}

	// If there is a wrap candidate, return its focus inactive view
	if (wrap_candidate) {
		struct wmiiv_container *wrap_inactive = seat_get_focus_inactive_view(
				seat, &wrap_candidate->node);
		return &wrap_inactive->node;
	}

	return NULL;
}

static struct wmiiv_node *node_get_in_direction_floating(
		struct wmiiv_container *con, struct wmiiv_seat *seat,
		enum wlr_direction dir) {
	double ref_lx = con->pending.x + con->pending.width / 2;
	double ref_ly = con->pending.y + con->pending.height / 2;
	double closest_distance = DBL_MAX;
	struct wmiiv_container *closest_con = NULL;

	if (!con->pending.workspace) {
		return NULL;
	}

	for (int i = 0; i < con->pending.workspace->floating->length; i++) {
		struct wmiiv_container *floater = con->pending.workspace->floating->items[i];
		if (floater == con) {
			continue;
		}
		float distance = dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT
			? (floater->pending.x + floater->pending.width / 2) - ref_lx
			: (floater->pending.y + floater->pending.height / 2) - ref_ly;
		if (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_UP) {
			distance = -distance;
		}
		if (distance < 0) {
			continue;
		}
		if (distance < closest_distance) {
			closest_distance = distance;
			closest_con = floater;
		}
	}

	return closest_con ? &closest_con->node : NULL;
}

static struct cmd_results *focus_mode(struct wmiiv_workspace *ws,
		struct wmiiv_seat *seat, bool floating) {
	struct wmiiv_container *new_focus = NULL;
	if (floating) {
		new_focus = seat_get_focus_inactive_floating(seat, ws);
	} else {
		new_focus = seat_get_focus_inactive_tiling(seat, ws);
	}
	if (new_focus) {
		seat_set_focus_window(seat, new_focus);

		// If we're on the floating layer and the floating container area
		// overlaps the position on the tiling layer that would be warped to,
		// `seat_consider_warp_to_focus` would decide not to warp, but we need
		// to anyway.
		if (config->mouse_warping == WARP_CONTAINER) {
			cursor_warp_to_container(seat->cursor, new_focus, true);
		} else {
			seat_consider_warp_to_focus(seat);
		}
	} else {
		return cmd_results_new(CMD_FAILURE,
				"Failed to find a %s container in workspace.",
				floating ? "floating" : "tiling");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *focus_output(struct wmiiv_seat *seat,
		int argc, char **argv) {
	if (!argc) {
		return cmd_results_new(CMD_INVALID,
			"Expected 'focus output <direction|name>'.");
	}
	char *identifier = join_args(argv, argc);
	struct wmiiv_output *output = output_by_name_or_id(identifier);

	if (!output) {
		enum wlr_direction direction;
		if (!parse_direction(identifier, &direction)) {
			free(identifier);
			return cmd_results_new(CMD_INVALID,
				"There is no output with that name.");
		}
		struct wmiiv_workspace *ws = seat_get_focused_workspace(seat);
		if (!ws) {
			free(identifier);
			return cmd_results_new(CMD_FAILURE,
				"No focused workspace to base directions off of.");
		}
		output = output_get_in_direction(ws->output, direction);

		if (!output) {
			int center_lx = ws->output->lx + ws->output->width / 2;
			int center_ly = ws->output->ly + ws->output->height / 2;
			struct wlr_output *target = wlr_output_layout_farthest_output(
					root->output_layout, opposite_direction(direction),
					ws->output->wlr_output, center_lx, center_ly);
			if (target) {
				output = output_from_wlr_output(target);
			}
		}
	}

	free(identifier);
	if (output) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &output->node));
		seat_consider_warp_to_focus(seat);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_focus(int argc, char **argv) {
	if (config->reading || !config->active) {
		return cmd_results_new(CMD_DEFER, NULL);
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct wmiiv_node *node = config->handler_context.node;
	struct wmiiv_workspace *workspace = config->handler_context.workspace;
	struct wmiiv_container *win = config->handler_context.window;
	struct wmiiv_seat *seat = config->handler_context.seat;
	if (node->type < N_WORKSPACE) {
		return cmd_results_new(CMD_FAILURE,
			"Command 'focus' cannot be used above the workspace level.");
	}

	if (argc == 0) {
		if (!win) {
			return cmd_results_new(CMD_INVALID, "No window to focus was specified.");
		}

		// if we are switching to a container under a fullscreen window, we first
		// need to exit fullscreen so that the newly focused container becomes visible
		struct wmiiv_container *obstructing = container_obstructing_fullscreen_container(win);
		if (obstructing) {
			container_fullscreen_disable(obstructing);
			arrange_root();
		}
		seat_set_focus_window(seat, win);
		seat_consider_warp_to_focus(seat);
		container_raise_floating(win);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (strcmp(argv[0], "floating") == 0) {
		return focus_mode(workspace, seat, true);
	} else if (strcmp(argv[0], "tiling") == 0) {
		return focus_mode(workspace, seat, false);
	} else if (strcmp(argv[0], "mode_toggle") == 0) {
		bool floating = win && window_is_floating(win);
		return focus_mode(workspace, seat, !floating);
	}

	if (strcmp(argv[0], "output") == 0) {
		argc--; argv++;
		return focus_output(seat, argc, argv);
	}

	enum wlr_direction direction = 0;
	if (!parse_direction(argv[0], &direction)) {
		if (!get_direction_from_next_prev(win, seat, argv[0], &direction)) {
			return cmd_results_new(CMD_INVALID,
				"Expected 'focus <direction|next|prev|mode_toggle|floating|tiling>' "
				"or 'focus output <direction|name>'");
		}
	}

	if (!direction) {
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (node->type == N_WORKSPACE) {
		// Jump to the next output
		struct wmiiv_output *new_output =
			output_get_in_direction(workspace->output, direction);
		if (!new_output) {
			return cmd_results_new(CMD_SUCCESS, NULL);
		}

		struct wmiiv_node *node =
			get_node_in_output_direction(new_output, direction);
		seat_set_focus(seat, node);
		seat_consider_warp_to_focus(seat);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (!wmiiv_assert(win, "Expected container to be non null")) {
		return cmd_results_new(CMD_FAILURE, "");
	}
	struct wmiiv_node *next_focus = NULL;
	if (window_is_floating(win) &&
			win->pending.fullscreen_mode == FULLSCREEN_NONE) {
		next_focus = node_get_in_direction_floating(win, seat, direction);
	} else {
		next_focus = node_get_in_direction_tiling(win, seat, direction);
	}
	if (next_focus) {
		seat_set_focus(seat, next_focus);
		seat_consider_warp_to_focus(seat);

		if (next_focus->type == N_COLUMN || next_focus->type == N_WINDOW) {
			container_raise_floating(next_focus->wmiiv_container);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
