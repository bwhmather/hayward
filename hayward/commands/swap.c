#define _POSIX_C_SOURCE 200809L
#include <strings.h>
#include "config.h"
#include "log.h"
#include "hayward/commands.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/root.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "stringop.h"

static const char expected_syntax[] =
	"Expected 'swap container with id|container_id|mark <arg>'";

static void swap_places(struct hayward_window *window1,
		struct hayward_window *window2) {
	struct hayward_window *temp = malloc(sizeof(struct hayward_window));
	temp->pending.x = window1->pending.x;
	temp->pending.y = window1->pending.y;
	temp->pending.width = window1->pending.width;
	temp->pending.height = window1->pending.height;
	temp->width_fraction = window1->width_fraction;
	temp->height_fraction = window1->height_fraction;
	temp->pending.parent = window1->pending.parent;
	temp->pending.workspace = window1->pending.workspace;
	bool temp_floating = window_is_floating(window1);

	window1->pending.x = window2->pending.x;
	window1->pending.y = window2->pending.y;
	window1->pending.width = window2->pending.width;
	window1->pending.height = window2->pending.height;
	window1->width_fraction = window2->width_fraction;
	window1->height_fraction = window2->height_fraction;

	window2->pending.x = temp->pending.x;
	window2->pending.y = temp->pending.y;
	window2->pending.width = temp->pending.width;
	window2->pending.height = temp->pending.height;
	window2->width_fraction = temp->width_fraction;
	window2->height_fraction = temp->height_fraction;

	int temp_index = window_sibling_index(window1);
	window_detach(window1);
	if (window2->pending.parent) {
		column_insert_child(window2->pending.parent, window1,
				window_sibling_index(window2));
	} else if (window_is_floating(window2)) {
		workspace_add_floating(window2->pending.workspace, window1);
	} else {
		hayward_abort("Window must either be floating or have a parent");
	}

	window_detach(window2);
	if (temp->pending.parent) {
		column_insert_child(temp->pending.parent, window2, temp_index);
	} else if (temp_floating) {
		workspace_add_floating(temp->pending.workspace, window2);
	} else {
		hayward_abort("Window must either be floating or have a parent");
	}

	free(temp);
}

static void swap_focus(struct hayward_window *window1,
		struct hayward_window *window2, struct hayward_seat *seat,
		struct hayward_window *focus) {
	if (focus == window1 || focus == window2) {
		struct hayward_workspace *workspace1 = window1->pending.workspace;
		struct hayward_workspace *workspace2 = window2->pending.workspace;
		enum hayward_column_layout layout1 = window_parent_layout(window1);
		enum hayward_column_layout layout2 = window_parent_layout(window2);
		if (focus == window1 && layout2 == L_STACKED) {
			if (workspace_is_visible(workspace2)) {
				seat_set_raw_focus(seat, &window2->node);
			}
			seat_set_focus_window(seat, workspace1 != workspace2 ? window2 : window1);
		} else if (focus == window2 && layout1 == L_STACKED) {
			if (workspace_is_visible(workspace1)) {
				seat_set_raw_focus(seat, &window1->node);
			}
			seat_set_focus_window(seat, workspace1 != workspace2 ? window1 : window2);
		} else if (workspace1 != workspace2) {
			seat_set_focus_window(seat, focus == window1 ? window2 : window1);
		} else {
			seat_set_focus_window(seat, focus);
		}
	} else {
		seat_set_focus_window(seat, focus);
	}

	if (root->fullscreen_global) {
		seat_set_focus(seat,
				seat_get_focus_inactive(seat, &root->fullscreen_global->node));
	}
}

void window_swap(struct hayward_window *window1, struct hayward_window *window2) {
	hayward_assert(window1 && window2, "Cannot swap with nothing");

	hayward_log(HAYWARD_DEBUG, "Swapping containers %zu and %zu",
			window1->node.id, window2->node.id);

	enum hayward_fullscreen_mode fs1 = window1->pending.fullscreen_mode;
	if (fs1) {
		window_fullscreen_disable(window1);
	}
	enum hayward_fullscreen_mode fs2 = window2->pending.fullscreen_mode;
	if (fs2) {
		window_fullscreen_disable(window2);
	}

	struct hayward_seat *seat = config->handler_context.seat;
	struct hayward_window *focus = seat_get_focused_container(seat);
	struct hayward_workspace *vis1 =
		output_get_active_workspace(window1->pending.workspace->output);
	struct hayward_workspace *vis2 =
		output_get_active_workspace(window2->pending.workspace->output);
	hayward_assert(vis1 && vis2, "window1 or window2 are on an output without a"
				"workspace. This should not happen");

	char *stored_prev_name = NULL;
	if (seat->prev_workspace_name) {
		stored_prev_name = strdup(seat->prev_workspace_name);
	}

	swap_places(window1, window2);

	if (!workspace_is_visible(vis1)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &vis1->node));
	}
	if (!workspace_is_visible(vis2)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &vis2->node));
	}

	swap_focus(window1, window2, seat, focus);

	if (stored_prev_name) {
		free(seat->prev_workspace_name);
		seat->prev_workspace_name = stored_prev_name;
	}

	if (fs1) {
		window_set_fullscreen(window2, fs1);
	}
	if (fs2) {
		window_set_fullscreen(window1, fs2);
	}
}

static bool test_container_id(struct hayward_window *container, void *data) {
	size_t *container_id = data;
	return container->node.id == *container_id;
}

#if HAVE_XWAYLAND
static bool test_id(struct hayward_window *container, void *data) {
	xcb_window_t *wid = data;
	return (container->view && container->view->type == HAYWARD_VIEW_XWAYLAND
			&& container->view->wlr_xwayland_surface->window_id == *wid);
}
#endif

static bool test_mark(struct hayward_window *container, void *mark) {
	if (container->marks->length) {
		return list_seq_find(container->marks,
				(int (*)(const void *, const void *))strcmp, mark) != -1;
	}
	return false;
}

struct cmd_results *cmd_swap(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "swap", EXPECTED_AT_LEAST, 4))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}

	if (strcasecmp(argv[0], "container") || strcasecmp(argv[1], "with")) {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	struct hayward_window *current = config->handler_context.window;
	struct hayward_window *other = NULL;

	char *value = join_args(argv + 3, argc - 3);
	if (strcasecmp(argv[2], "id") == 0) {
#if HAVE_XWAYLAND
		xcb_window_t id = strtol(value, NULL, 0);
		other = root_find_window(test_id, &id);
#endif
	} else if (strcasecmp(argv[2], "container_id") == 0) {
		size_t container_id = atoi(value);
		other = root_find_window(test_container_id, &container_id);
	} else if (strcasecmp(argv[2], "mark") == 0) {
		other = root_find_window(test_mark, value);
	} else {
		free(value);
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (!other) {
		error = cmd_results_new(CMD_FAILURE,
				"Failed to find %s '%s'", argv[2], value);
	} else if (!current) {
		error = cmd_results_new(CMD_FAILURE,
				"Can only swap with windows");
	} else if (current == other) {
		error = cmd_results_new(CMD_FAILURE,
				"Cannot swap a container with itself");
	}

	free(value);

	if (error) {
		return error;
	}

	window_swap(current, other);

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		struct hayward_node *current_parent = node_get_parent(&current->node);
		struct hayward_node *other_parent = node_get_parent(&other->node);
		if (current_parent) {
			arrange_node(current_parent);
		}
		if (other_parent && current_parent != other_parent) {
			arrange_node(other_parent);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
