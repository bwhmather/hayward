#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "wmiiv/commands.h"
#include "wmiiv/input/cursor.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/root.h"
#include "wmiiv/tree/workspace.h"
#include "stringop.h"
#include "list.h"
#include "log.h"
#include "util.h"

static const char expected_syntax[] =
	"Expected 'move <left|right|up|down> <[px] px>' or "
	"'move [--no-auto-back-and-forth] <container|window> [to] workspace <name>' or "
	"'move <container|window|workspace> [to] output <name|direction>' or "
	"'move <container|window> [to] mark <mark>'";

static struct wmiiv_output *output_in_direction(const char *direction_string,
		struct wmiiv_output *reference, int ref_lx, int ref_ly) {
	if (strcasecmp(direction_string, "current") == 0) {
		struct wmiiv_workspace *active_ws =
			seat_get_focused_workspace(config->handler_context.seat);
		if (!active_ws) {
			return NULL;
		}
		return active_ws->output;
	}

	struct {
		char *name;
		enum wlr_direction direction;
	} names[] = {
		{ "up", WLR_DIRECTION_UP },
		{ "down", WLR_DIRECTION_DOWN },
		{ "left", WLR_DIRECTION_LEFT },
		{ "right", WLR_DIRECTION_RIGHT },
	};

	enum wlr_direction direction = 0;

	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
		if (strcasecmp(names[i].name, direction_string) == 0) {
			direction = names[i].direction;
			break;
		}
	}

	if (reference && direction) {
		struct wlr_output *target = wlr_output_layout_adjacent_output(
				root->output_layout, direction, reference->wlr_output,
				ref_lx, ref_ly);

		if (!target) {
			target = wlr_output_layout_farthest_output(
					root->output_layout, opposite_direction(direction),
					reference->wlr_output, ref_lx, ref_ly);
		}

		if (target) {
			return target->data;
		}
	}

	return output_by_name_or_id(direction_string);
}

static bool container_move_to_next_output(struct wmiiv_container *container,
		struct wmiiv_output *output, enum wlr_direction move_dir) {
	struct wmiiv_output *next_output =
		output_get_in_direction(output, move_dir);
	if (next_output) {
		struct wmiiv_workspace *ws = output_get_active_workspace(next_output);
		if (!wmiiv_assert(ws, "Expected output to have a workspace")) {
			return false;
		}
		switch (container->pending.fullscreen_mode) {
		case FULLSCREEN_NONE:
			window_move_to_workspace_from_direction(container, ws, move_dir);
			return true;
		case FULLSCREEN_WORKSPACE:
			window_move_to_workspace(container, ws);
			return true;
		case FULLSCREEN_GLOBAL:
			return false;
		}
	}
	return false;
}

// Returns true if moved
static bool window_move_in_direction(struct wmiiv_container *window,
		enum wlr_direction move_dir) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return false;
	}

	// If moving a fullscreen view, only consider outputs
	switch (window->pending.fullscreen_mode) {
	case FULLSCREEN_NONE:
		break;
	case FULLSCREEN_WORKSPACE:
		return container_move_to_next_output(window,
				window->pending.workspace->output, move_dir);
	case FULLSCREEN_GLOBAL:
		return false;
	}

	if (window_is_floating(window)) {
		return false;
	}

	// TODO (wmiiv) windows should always have a parent if not floating.
	if (!window->pending.parent) {
		return false;
	}

	struct wmiiv_container *old_col = window->pending.parent;
	int old_col_index = list_find(window->pending.workspace->tiling, old_col);

	switch (move_dir) {
	case WLR_DIRECTION_UP: {
			// Move within column.
			// TODO
			return false;
		}
	case WLR_DIRECTION_DOWN: {
			// Move within column.
			// TODO
			return false;
		}
	case WLR_DIRECTION_LEFT: {
			if (old_col_index == 0) {
				// Window is already in the left most column.
				// If window is the only child of this column
				// then attempt to move it to the next
				// workspace, otherwise insert a new column to
				// the left and carry on as before.
				if (old_col->pending.children->length == 1) {
					// No other windows.  Move to next
					// workspace.

					return container_move_to_next_output(window,
						window->pending.workspace->output, move_dir);
				}

				struct wmiiv_container *new_col = column_create();
				new_col->pending.height = new_col->pending.width = 0;
				new_col->height_fraction = new_col->width_fraction = 0;
				new_col->pending.layout = L_STACKED;

				workspace_insert_tiling_direct(window->pending.workspace, new_col, 0);
				old_col_index += 1;
			}

			struct wmiiv_container *new_col = window->pending.workspace->tiling->items[old_col_index - 1];
			window_move_to_column_from_direction(window, new_col, move_dir);

			return true;
		}
	case WLR_DIRECTION_RIGHT: {
			if (old_col_index == window->pending.workspace->tiling->length - 1) {
				// Window is already in the right most column.
				// If window is the only child of this column
				// then attempt to move it to the next
				// workspace, otherwise insert a new column to
				// the right and carry on as before.
				if (old_col->pending.children->length == 1) {
					// TODO find then move should be separate calls at this level of abstraction.
					return container_move_to_next_output(window,
						window->pending.workspace->output, move_dir);
				}

				struct wmiiv_container *new_col = column_create();
				new_col->pending.height = new_col->pending.width = 0;
				new_col->height_fraction = new_col->width_fraction = 0;
				new_col->pending.layout = L_STACKED;

				workspace_insert_tiling_direct(window->pending.workspace, new_col, old_col_index + 1);
			}

			struct wmiiv_container *new_col = window->pending.workspace->tiling->items[old_col_index + 1];
			window_move_to_column_from_direction(window, new_col, move_dir);

			return true;
		}
	}
	return false;  // TODO unreachable.
}

static struct cmd_results *cmd_move_container(bool no_auto_back_and_forth,
		int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move container/window",
				EXPECTED_AT_LEAST, 2))) {
		return error;
	}

	struct wmiiv_container *window = config->handler_context.window;

	if (!window) {
		return cmd_results_new(CMD_FAILURE, "Can only move windows");
	}

	if (window->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		return cmd_results_new(CMD_FAILURE,
				"Can't move fullscreen global container");
	}

	struct wmiiv_seat *seat = config->handler_context.seat;
	struct wmiiv_container *old_parent = window->pending.parent;
	struct wmiiv_workspace *old_ws = window->pending.workspace;
	struct wmiiv_output *old_output = old_ws ? old_ws->output : NULL;
	struct wmiiv_node *destination = NULL;

	// determine destination
	if (strcasecmp(argv[0], "workspace") == 0) {
		// Determine which workspace the window should be moved to.
		struct wmiiv_workspace *ws = NULL;
		char *ws_name = NULL;
		if (strcasecmp(argv[1], "next") == 0 ||
				strcasecmp(argv[1], "prev") == 0 ||
				strcasecmp(argv[1], "next_on_output") == 0 ||
				strcasecmp(argv[1], "prev_on_output") == 0 ||
				strcasecmp(argv[1], "current") == 0) {
			ws = workspace_by_name(argv[1]);
		} else if (strcasecmp(argv[1], "back_and_forth") == 0) {
			if (!(ws = workspace_by_name(argv[1]))) {
				if (seat->prev_workspace_name) {
					ws_name = strdup(seat->prev_workspace_name);
				} else {
					return cmd_results_new(CMD_FAILURE,
							"No workspace was previously active.");
				}
			}
		} else {
			if (strcasecmp(argv[1], "number") == 0) {
				// move [window|container] [to] "workspace number x"
				if (argc < 3) {
					return cmd_results_new(CMD_INVALID, expected_syntax);
				}
				if (!isdigit(argv[2][0])) {
					return cmd_results_new(CMD_INVALID,
							"Invalid workspace number '%s'", argv[2]);
				}
				ws_name = join_args(argv + 2, argc - 2);
				ws = workspace_by_number(ws_name);
			} else {
				ws_name = join_args(argv + 1, argc - 1);
				ws = workspace_by_name(ws_name);
			}

			if (!no_auto_back_and_forth && config->auto_back_and_forth &&
					seat->prev_workspace_name) {
				// auto back and forth move
				if (old_ws && old_ws->name &&
						strcmp(old_ws->name, ws_name) == 0) {
					// if target workspace is the current one
					free(ws_name);
					ws_name = strdup(seat->prev_workspace_name);
					ws = workspace_by_name(ws_name);
				}
			}
		}
		if (!ws) {
			// We have to create the workspace, but if the container is
			// sticky and the workspace is going to be created on the same
			// output, we'll bail out first.
			if (container_is_sticky_or_child(window)) {
				struct wmiiv_output *new_output =
					workspace_get_initial_output(ws_name);
				if (old_output == new_output) {
					free(ws_name);
					return cmd_results_new(CMD_FAILURE,
							"Can't move sticky container to another workspace "
							"on the same output");
				}
			}
			ws = workspace_create(NULL, ws_name);
		}
		free(ws_name);

		// Do the move.
		window_move_to_workspace(window, ws);

		ipc_event_window(window, "move");

		// Restore focus to the original workspace.
		struct wmiiv_container *focus = seat_get_focus_inactive_view(seat, &old_ws->node);
		if (focus) {
			seat_set_focus_window(seat, focus);
		} else {
			seat_set_focus_workspace(seat, old_ws);
		}

		// If necessary, clean up old column and workspace.
		if (old_parent) {
			column_consider_destroy(old_parent);
		}
		if (old_ws) {
			workspace_consider_destroy(old_ws);
		}


		// Re-arrange windows
		if (root->fullscreen_global) {
			arrange_root();
		} else {
			if (old_ws && !old_ws->node.destroying) {
				arrange_workspace(old_ws);
			}
			// TODO (wmiiv) it should often be possible to get away without rearranging
			// the entire workspace.
			arrange_workspace(ws);
		}

		return cmd_results_new(CMD_SUCCESS, NULL);

	} else if (strcasecmp(argv[0], "output") == 0) {
		struct wmiiv_output *new_output = output_in_direction(argv[1],
				old_output, window->pending.x, window->pending.y);
		if (!new_output) {
			return cmd_results_new(CMD_FAILURE,
				"Can't find output with name/direction '%s'", argv[1]);
		}
		destination = seat_get_focus_inactive(seat, &new_output->node);
	} else if (strcasecmp(argv[0], "mark") == 0) {
		struct wmiiv_container *dest_container = window_find_mark(argv[1]);
		if (dest_container == NULL) {
			return cmd_results_new(CMD_FAILURE,
					"Mark '%s' not found", argv[1]);
		}
		destination = &dest_container->node;
	} else {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (container_is_sticky_or_child(window) && old_output &&
			node_has_ancestor(destination, &old_output->node)) {
		return cmd_results_new(CMD_FAILURE, "Can't move sticky "
				"container to another workspace on the same output");
	}

	struct wmiiv_output *new_output = node_get_output(destination);
	struct wmiiv_workspace *new_output_last_ws = NULL;
	if (new_output && old_output != new_output) {
		new_output_last_ws = output_get_active_workspace(new_output);
	}

	// save focus, in case it needs to be restored
	struct wmiiv_node *focus = seat_get_focus(seat);

	switch (destination->type) {
	case N_WORKSPACE:
		window_move_to_workspace(window, destination->wmiiv_workspace);
		break;
	case N_OUTPUT: {
			struct wmiiv_output *output = destination->wmiiv_output;
			struct wmiiv_workspace *ws = output_get_active_workspace(output);
			if (!wmiiv_assert(ws, "Expected output to have a workspace")) {
				return cmd_results_new(CMD_FAILURE,
						"Expected output to have a workspace");
			}
			window_move_to_workspace(window, ws);
		}
		break;
	case N_WINDOW:
		// TODO (wmiiv)
	case N_COLUMN:
		window_move_to_column(window, destination->wmiiv_container);
		break;
	case N_ROOT:
		break;
	}

	ipc_event_window(window, "move");

	// restore focus on destination output back to its last active workspace
	struct wmiiv_workspace *new_workspace = new_output ?
		output_get_active_workspace(new_output) : NULL;
	if (new_output &&
			!wmiiv_assert(new_workspace, "Expected output to have a workspace")) {
		return cmd_results_new(CMD_FAILURE,
				"Expected output to have a workspace");
	}

	if (new_output_last_ws && new_output_last_ws != new_workspace) {
		struct wmiiv_node *new_output_last_focus =
			seat_get_focus_inactive(seat, &new_output_last_ws->node);
		seat_set_raw_focus(seat, new_output_last_focus);
	}

	// restore focus
	if (focus == &window->node) {
		focus = NULL;
		if (old_parent) {
			focus = seat_get_focus_inactive(seat, &old_parent->node);
		}
		if (!focus && old_ws) {
			focus = seat_get_focus_inactive(seat, &old_ws->node);
		}
	}
	seat_set_focus(seat, focus);

	// clean-up, destroying parents if the container was the last child
	if (old_parent) {
		column_consider_destroy(old_parent);
	}
	if (old_ws) {
		workspace_consider_destroy(old_ws);
	}

	// arrange windows
	if (root->fullscreen_global) {
		arrange_root();
	} else {
		if (old_ws && !old_ws->node.destroying) {
			arrange_workspace(old_ws);
		}
		arrange_node(node_get_parent(destination));
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static void workspace_move_to_output(struct wmiiv_workspace *workspace,
		struct wmiiv_output *output) {
	if (workspace->output == output) {
		return;
	}
	struct wmiiv_output *old_output = workspace->output;
	workspace_detach(workspace);
	struct wmiiv_workspace *new_output_old_ws =
		output_get_active_workspace(output);
	if (!wmiiv_assert(new_output_old_ws, "Expected output to have a workspace")) {
		return;
	}

	output_add_workspace(output, workspace);

	// If moving the last workspace from the old output, create a new workspace
	// on the old output
	struct wmiiv_seat *seat = config->handler_context.seat;
	if (old_output->workspaces->length == 0) {
		char *ws_name = workspace_next_name(old_output->wlr_output->name);
		struct wmiiv_workspace *ws = workspace_create(old_output, ws_name);
		free(ws_name);
		seat_set_raw_focus(seat, &ws->node);
	}

	workspace_consider_destroy(new_output_old_ws);

	output_sort_workspaces(output);
	struct wmiiv_node *focus = seat_get_focus_inactive(seat, &workspace->node);
	seat_set_focus(seat, focus);
	workspace_output_raise_priority(workspace, old_output, output);
	ipc_event_workspace(NULL, workspace, "move");
}

static struct cmd_results *cmd_move_workspace(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move workspace", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	if (strcasecmp(argv[0], "output") == 0) {
		--argc; ++argv;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID,
				"Expected 'move workspace to [output] <output>'");
	}

	struct wmiiv_workspace *workspace = config->handler_context.workspace;
	if (!workspace) {
		return cmd_results_new(CMD_FAILURE, "No workspace to move");
	}

	struct wmiiv_output *old_output = workspace->output;
	int center_x = workspace->width / 2 + workspace->x,
		center_y = workspace->height / 2 + workspace->y;
	struct wmiiv_output *new_output = output_in_direction(argv[0],
			old_output, center_x, center_y);
	if (!new_output) {
		return cmd_results_new(CMD_FAILURE,
			"Can't find output with name/direction '%s'", argv[0]);
	}
	workspace_move_to_output(workspace, new_output);

	arrange_output(old_output);
	arrange_output(new_output);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_in_direction(
		enum wlr_direction direction, int argc, char **argv) {
	int move_amt = 10;
	if (argc) {
		char *inv;
		move_amt = (int)strtol(argv[0], &inv, 10);
		if (*inv != '\0' && strcasecmp(inv, "px") != 0) {
			return cmd_results_new(CMD_FAILURE, "Invalid distance specified");
		}
	}

	struct wmiiv_container *window = config->handler_context.window;
	if (!window) {
		return cmd_results_new(CMD_FAILURE,
				"Cannot move workspaces in a direction");
	}
	if (window_is_floating(window)) {
		if (window->pending.fullscreen_mode) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move fullscreen floating window");
		}
		double lx = window->pending.x;
		double ly = window->pending.y;
		switch (direction) {
		case WLR_DIRECTION_LEFT:
			lx -= move_amt;
			break;
		case WLR_DIRECTION_RIGHT:
			lx += move_amt;
			break;
		case WLR_DIRECTION_UP:
			ly -= move_amt;
			break;
		case WLR_DIRECTION_DOWN:
			ly += move_amt;
			break;
		}
		container_floating_move_to(window, lx, ly);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}
	struct wmiiv_workspace *old_ws = window->pending.workspace;
	struct wmiiv_container *old_parent = window->pending.parent;

	if (!window_move_in_direction(window, direction)) {
		// Container didn't move
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	// clean-up, destroying parents if the container was the last child
	if (old_parent) {
		column_consider_destroy(old_parent);
	} else if (old_ws) {
		// TODO (wmiiv) shouldn't be possible to hit this.
		workspace_consider_destroy(old_ws);
	}

	struct wmiiv_workspace *new_ws = window->pending.workspace;

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		arrange_workspace(old_ws);
		if (new_ws != old_ws) {
			arrange_workspace(new_ws);
		}
	}

	ipc_event_window(window, "move");

	// Hack to re-focus container
	seat_set_raw_focus(config->handler_context.seat, &new_ws->node);
	seat_set_focus_window(config->handler_context.seat, window);

	if (old_ws != new_ws) {
		ipc_event_workspace(old_ws, new_ws, "focus");
		workspace_detect_urgent(old_ws);
		workspace_detect_urgent(new_ws);
	}
	container_end_mouse_operation(window);

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *cmd_move_to_position_pointer(
		struct wmiiv_container *container) {
	struct wmiiv_seat *seat = config->handler_context.seat;
	if (!seat->cursor) {
		return cmd_results_new(CMD_FAILURE, "No cursor device");
	}
	struct wlr_cursor *cursor = seat->cursor->cursor;
	/* Determine where to put the window. */
	double lx = cursor->x - container->pending.width / 2;
	double ly = cursor->y - container->pending.height / 2;

	/* Correct target coordinates to be in bounds (on screen). */
	struct wlr_output *output = wlr_output_layout_output_at(
			root->output_layout, cursor->x, cursor->y);
	if (output) {
		struct wlr_box box;
		wlr_output_layout_get_box(root->output_layout, output, &box);
		lx = fmax(lx, box.x);
		ly = fmax(ly, box.y);
		if (lx + container->pending.width > box.x + box.width) {
			lx = box.x + box.width - container->pending.width;
		}
		if (ly + container->pending.height > box.y + box.height) {
			ly = box.y + box.height - container->pending.height;
		}
	}

	/* Actually move the container. */
	container_floating_move_to(container, lx, ly);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_position_syntax[] =
	"Expected 'move [absolute] position <x> [px] <y> [px]' or "
	"'move [absolute] position center' or "
	"'move position cursor|mouse|pointer'";

static struct cmd_results *cmd_move_to_position(int argc, char **argv) {
	struct wmiiv_container *window = config->handler_context.window;
	if (!window || !window_is_floating(window)) {
		return cmd_results_new(CMD_FAILURE, "Only floating containers "
				"can be moved to an absolute position");
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}

	bool absolute = false;
	if (strcmp(argv[0], "absolute") == 0) {
		absolute = true;
		--argc;
		++argv;
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}
	if (strcmp(argv[0], "position") == 0) {
		--argc;
		++argv;
	}
	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}
	if (strcmp(argv[0], "cursor") == 0 || strcmp(argv[0], "mouse") == 0 ||
			strcmp(argv[0], "pointer") == 0) {
		if (absolute) {
			return cmd_results_new(CMD_INVALID, expected_position_syntax);
		}
		return cmd_move_to_position_pointer(window);
	} else if (strcmp(argv[0], "center") == 0) {
		double lx, ly;
		if (absolute) {
			lx = root->x + (root->width - window->pending.width) / 2;
			ly = root->y + (root->height - window->pending.height) / 2;
		} else {
			struct wmiiv_workspace *ws = window->pending.workspace;
			if (!ws) {
				struct wmiiv_seat *seat = config->handler_context.seat;
				ws = seat_get_focused_workspace(seat);
			}
			lx = ws->x + (ws->width - window->pending.width) / 2;
			ly = ws->y + (ws->height - window->pending.height) / 2;
		}
		container_floating_move_to(window, lx, ly);
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	if (argc < 2) {
		return cmd_results_new(CMD_FAILURE, expected_position_syntax);
	}

	struct movement_amount lx = { .amount = 0, .unit = MOVEMENT_UNIT_INVALID };
	// X direction
	int num_consumed_args = parse_movement_amount(argc, argv, &lx);
	argc -= num_consumed_args;
	argv += num_consumed_args;
	if (lx.unit == MOVEMENT_UNIT_INVALID) {
		return cmd_results_new(CMD_INVALID, "Invalid x position specified");
	}

	if (argc < 1) {
		return cmd_results_new(CMD_FAILURE, expected_position_syntax);
	}

	struct movement_amount ly = { .amount = 0, .unit = MOVEMENT_UNIT_INVALID };
	// Y direction
	num_consumed_args = parse_movement_amount(argc, argv, &ly);
	argc -= num_consumed_args;
	argv += num_consumed_args;
	if (argc > 0) {
		return cmd_results_new(CMD_INVALID, expected_position_syntax);
	}
	if (ly.unit == MOVEMENT_UNIT_INVALID) {
		return cmd_results_new(CMD_INVALID, "Invalid y position specified");
	}

	struct wmiiv_workspace *ws = window->pending.workspace;
	if (!ws) {
		struct wmiiv_seat *seat = config->handler_context.seat;
		ws = seat_get_focused_workspace(seat);
	}

	switch (lx.unit) {
	case MOVEMENT_UNIT_PPT:
		if (absolute) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move to absolute positions by ppt");
		}
		// Convert to px
		lx.amount = ws->width * lx.amount / 100;
		lx.unit = MOVEMENT_UNIT_PX;
		// Falls through
	case MOVEMENT_UNIT_PX:
	case MOVEMENT_UNIT_DEFAULT:
		break;
	case MOVEMENT_UNIT_INVALID:
		wmiiv_assert(false, "invalid x unit");
		break;
	}

	switch (ly.unit) {
	case MOVEMENT_UNIT_PPT:
		if (absolute) {
			return cmd_results_new(CMD_FAILURE,
					"Cannot move to absolute positions by ppt");
		}
		// Convert to px
		ly.amount = ws->height * ly.amount / 100;
		ly.unit = MOVEMENT_UNIT_PX;
		// Falls through
	case MOVEMENT_UNIT_PX:
	case MOVEMENT_UNIT_DEFAULT:
		break;
	case MOVEMENT_UNIT_INVALID:
		wmiiv_assert(false, "invalid y unit");
		break;
	}
	if (!absolute) {
		lx.amount += ws->x;
		ly.amount += ws->y;
	}
	container_floating_move_to(window, lx.amount, ly.amount);
	return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_full_syntax[] = "Expected "
	"'move left|right|up|down [<amount> [px]]'"
	" or 'move [--no-auto-back-and-forth] [window|container] [to] workspace"
	"  <name>|next|prev|next_on_output|prev_on_output|current|(number <num>)'"
	" or 'move [window|container] [to] output <name/id>|left|right|up|down'"
	" or 'move [window|container] [to] mark <mark>'"
	" or 'move workspace to [output] <name/id>|left|right|up|down'"
	" or 'move [window|container] [to] [absolute] position <x> [px] <y> [px]'"
	" or 'move [window|container] [to] [absolute] position center'"
	" or 'move [window|container] [to] position mouse|cursor|pointer'";

struct cmd_results *cmd_move(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "move", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	if (strcasecmp(argv[0], "left") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_LEFT, --argc, ++argv);
	} else if (strcasecmp(argv[0], "right") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_RIGHT, --argc, ++argv);
	} else if (strcasecmp(argv[0], "up") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_UP, --argc, ++argv);
	} else if (strcasecmp(argv[0], "down") == 0) {
		return cmd_move_in_direction(WLR_DIRECTION_DOWN, --argc, ++argv);
	} else if (strcasecmp(argv[0], "workspace") == 0 && argc >= 2
			&& (strcasecmp(argv[1], "to") == 0 ||
				strcasecmp(argv[1], "output") == 0)) {
		argc -= 2; argv += 2;
		return cmd_move_workspace(argc, argv);
	}

	bool no_auto_back_and_forth = false;
	if (strcasecmp(argv[0], "--no-auto-back-and-forth") == 0) {
		no_auto_back_and_forth = true;
		--argc; ++argv;
	}

	if (argc > 0 && (strcasecmp(argv[0], "window") == 0 ||
			strcasecmp(argv[0], "container") == 0)) {
		--argc; ++argv;
	}

	if (argc > 0 && strcasecmp(argv[0], "to") == 0) {
		--argc;	++argv;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID, expected_full_syntax);
	}

	// Only `move [window|container] [to] workspace` supports
	// `--no-auto-back-and-forth` so treat others as invalid syntax
	if (no_auto_back_and_forth && strcasecmp(argv[0], "workspace") != 0) {
		return cmd_results_new(CMD_INVALID, expected_full_syntax);
	}

	if (strcasecmp(argv[0], "workspace") == 0 ||
			strcasecmp(argv[0], "output") == 0 ||
			strcasecmp(argv[0], "mark") == 0) {
		return cmd_move_container(no_auto_back_and_forth, argc, argv);
	} else if (strcasecmp(argv[0], "position") == 0 ||
			(argc > 1 && strcasecmp(argv[0], "absolute") == 0 &&
			strcasecmp(argv[1], "position") == 0)) {
		return cmd_move_to_position(argc, argv);
	}
	return cmd_results_new(CMD_INVALID, expected_full_syntax);
}
