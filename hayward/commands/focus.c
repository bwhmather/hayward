#include <float.h>
#include <strings.h>
#include <wlr/types/wlr_output_layout.h>
#include "log.h"
#include "hayward/commands.h"
#include "hayward/input/input-manager.h"
#include "hayward/input/cursor.h"
#include "hayward/input/seat.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/root.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "stringop.h"
#include "util.h"

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
static struct hayward_node *get_node_in_output_direction(
		struct hayward_output *output, enum wlr_direction dir) {
	struct hayward_seat *seat = config->handler_context.seat;
	struct hayward_workspace *workspace = output_get_active_workspace(output);
	if (!hayward_assert(workspace, "Expected output to have a workspace")) {
		return NULL;
	}
	if (workspace->fullscreen) {
		return &workspace->fullscreen->node;
	}
	struct hayward_window *column = NULL;
	struct hayward_window *window = NULL;

	if (workspace->tiling->length > 0) {
		switch (dir) {
		case WLR_DIRECTION_LEFT:
			// get most right child of new output
			column = workspace->tiling->items[workspace->tiling->length-1];
			window = seat_get_focus_inactive_view(seat, &column->node);
			break;
		case WLR_DIRECTION_RIGHT:
			// get most left child of new output
			column = workspace->tiling->items[0];
			window = seat_get_focus_inactive_view(seat, &column->node);
			break;
		case WLR_DIRECTION_UP:
			window = seat_get_focus_inactive_tiling(seat, workspace);
			break;
		case WLR_DIRECTION_DOWN:
			window = seat_get_focus_inactive_tiling(seat, workspace);
			break;
		}
	}

	if (window) {
		return &window->node;
	}

	return &workspace->node;
}

static struct hayward_node *node_get_in_direction_tiling(
		struct hayward_window *window, struct hayward_seat *seat,
		enum wlr_direction dir) {
	struct hayward_window *wrap_candidate = NULL;

	if (window->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		// Fullscreen container with a direction - go straight to outputs
		struct hayward_output *output = window->pending.workspace->output;
		struct hayward_output *new_output =
			output_get_in_direction(output, dir);
		if (!new_output) {
			return NULL;
		}
		return get_node_in_output_direction(new_output, dir);
	}
	if (window->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		return NULL;
	}

	// TODO (hayward) this is a manually unrolled recursion over container.  Make it nice.
	// Window iteration.
	if (dir == WLR_DIRECTION_UP || dir == WLR_DIRECTION_DOWN) {
		// Try to move up and down within the current column.
		int current_idx = window_sibling_index(window);
		int desired_idx = current_idx + (dir == WLR_DIRECTION_UP ? -1 : 1);

		list_t *siblings = window_get_siblings(window);

		if (desired_idx >= 0 && desired_idx < siblings->length) {
			return siblings->items[desired_idx];
		}

		if (config->focus_wrapping != WRAP_NO && !wrap_candidate && siblings->length > 1) {
			if (desired_idx < 0) {
				wrap_candidate = siblings->items[siblings->length - 1];
			} else {
				wrap_candidate = siblings->items[0];
			}
			if (config->focus_wrapping == WRAP_FORCE) {
				return &wrap_candidate->node;
			}
		}
	} else {
		// Try to move to the next column to the left of right within
		// the current workspace.
		struct hayward_column *column = window->pending.parent;

		int current_idx = column_sibling_index(column);
		int desired_idx = current_idx + (dir == WLR_DIRECTION_LEFT ? -1 : 1);

		list_t *siblings = column_get_siblings(column);

		if (desired_idx >= 0 && desired_idx < siblings->length) {
			struct hayward_column *next_column = siblings->items[desired_idx];
			struct hayward_window *next_window = seat_get_focus_inactive_view(seat, &next_column->node);
			return &next_window->node;
		}

		if (config->focus_wrapping != WRAP_NO && !wrap_candidate && siblings->length > 1) {
			struct hayward_window *wrap_candidate_column;
			if (desired_idx < 0) {
				wrap_candidate_column = siblings->items[siblings->length - 1];
			} else {
				wrap_candidate_column = siblings->items[0];
			}
			wrap_candidate = seat_get_focus_inactive_view(seat, &wrap_candidate_column->node);
			if (config->focus_wrapping == WRAP_FORCE) {
				return &wrap_candidate->node;
			}
		}
	}

	// Check a different output
	struct hayward_output *output = window->pending.workspace->output;
	struct hayward_output *new_output = output_get_in_direction(output, dir);
	if (config->focus_wrapping != WRAP_WORKSPACE && new_output) {
		return get_node_in_output_direction(new_output, dir);
	}

	// If there is a wrap candidate, return its focus inactive view
	if (wrap_candidate) {
		struct hayward_window *wrap_inactive = seat_get_focus_inactive_view(
				seat, &wrap_candidate->node);
		return &wrap_inactive->node;
	}

	return NULL;
}

static struct hayward_node *node_get_in_direction_floating(
		struct hayward_window *container, struct hayward_seat *seat,
		enum wlr_direction dir) {
	double ref_lx = container->pending.x + container->pending.width / 2;
	double ref_ly = container->pending.y + container->pending.height / 2;
	double closest_distance = DBL_MAX;
	struct hayward_window *closest_container = NULL;

	if (!container->pending.workspace) {
		return NULL;
	}

	for (int i = 0; i < container->pending.workspace->floating->length; i++) {
		struct hayward_window *floater = container->pending.workspace->floating->items[i];
		if (floater == container) {
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
			closest_container = floater;
		}
	}

	return closest_container ? &closest_container->node : NULL;
}

static struct cmd_results *focus_mode(struct hayward_workspace *workspace,
		struct hayward_seat *seat, bool floating) {
	struct hayward_window *new_focus = NULL;
	if (floating) {
		new_focus = seat_get_focus_inactive_floating(seat, workspace);
	} else {
		new_focus = seat_get_focus_inactive_tiling(seat, workspace);
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

static struct cmd_results *focus_output(struct hayward_seat *seat,
		int argc, char **argv) {
	if (!argc) {
		return cmd_results_new(CMD_INVALID,
			"Expected 'focus output <direction|name>'.");
	}
	char *identifier = join_args(argv, argc);
	struct hayward_output *output = output_by_name_or_id(identifier);

	if (!output) {
		enum wlr_direction direction;
		if (!parse_direction(identifier, &direction)) {
			free(identifier);
			return cmd_results_new(CMD_INVALID,
				"There is no output with that name.");
		}
		struct hayward_workspace *workspace = seat_get_focused_workspace(seat);
		if (!workspace) {
			free(identifier);
			return cmd_results_new(CMD_FAILURE,
				"No focused workspace to base directions off of.");
		}
		output = output_get_in_direction(workspace->output, direction);

		if (!output) {
			int center_lx = workspace->output->lx + workspace->output->width / 2;
			int center_ly = workspace->output->ly + workspace->output->height / 2;
			struct wlr_output *target = wlr_output_layout_farthest_output(
					root->output_layout, opposite_direction(direction),
					workspace->output->wlr_output, center_lx, center_ly);
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
	struct hayward_node *node = config->handler_context.node;
	struct hayward_workspace *workspace = config->handler_context.workspace;
	struct hayward_window *window = config->handler_context.window;
	struct hayward_seat *seat = config->handler_context.seat;
	if (node->type < N_WORKSPACE) {
		return cmd_results_new(CMD_FAILURE,
			"Command 'focus' cannot be used above the workspace level.");
	}

	if (argc == 0) {
		if (!window) {
			return cmd_results_new(CMD_INVALID, "No window to focus was specified.");
		}

		// if we are switching to a container under a fullscreen window, we first
		// need to exit fullscreen so that the newly focused container becomes visible
		struct hayward_window *obstructing = window_obstructing_fullscreen_window(window);
		if (obstructing) {
			window_fullscreen_disable(obstructing);
			arrange_root();
		}
		seat_set_focus_window(seat, window);
		seat_consider_warp_to_focus(seat);
		window_raise_floating(window);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (strcmp(argv[0], "floating") == 0) {
		return focus_mode(workspace, seat, true);
	} else if (strcmp(argv[0], "tiling") == 0) {
		return focus_mode(workspace, seat, false);
	} else if (strcmp(argv[0], "mode_toggle") == 0) {
		bool floating = window && window_is_floating(window);
		return focus_mode(workspace, seat, !floating);
	}

	if (strcmp(argv[0], "output") == 0) {
		argc--; argv++;
		return focus_output(seat, argc, argv);
	}

	enum wlr_direction direction = 0;
	if (!parse_direction(argv[0], &direction)) {
		return cmd_results_new(CMD_INVALID,
			"Expected 'focus <direction|mode_toggle|floating|tiling>' "
			"or 'focus output <direction|name>'");
	}

	if (!direction) {
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (node->type == N_WORKSPACE) {
		// Jump to the next output
		struct hayward_output *new_output =
			output_get_in_direction(workspace->output, direction);
		if (!new_output) {
			return cmd_results_new(CMD_SUCCESS, NULL);
		}

		struct hayward_node *node =
			get_node_in_output_direction(new_output, direction);
		seat_set_focus(seat, node);
		seat_consider_warp_to_focus(seat);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (!hayward_assert(window, "Expected container to be non null")) {
		return cmd_results_new(CMD_FAILURE, "");
	}
	struct hayward_node *next_focus = NULL;
	if (window_is_floating(window) &&
			window->pending.fullscreen_mode == FULLSCREEN_NONE) {
		next_focus = node_get_in_direction_floating(window, seat, direction);
	} else {
		next_focus = node_get_in_direction_tiling(window, seat, direction);
	}
	if (next_focus) {
		hayward_assert(next_focus->type != N_COLUMN, "Shouldn't focus columns");
		hayward_assert(next_focus->type != N_WORKSPACE, "Shouldn't focus workspaces");
		seat_set_focus(seat, next_focus);
		seat_consider_warp_to_focus(seat);
		window_raise_floating(next_focus->hayward_window);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
