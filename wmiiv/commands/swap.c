#define _POSIX_C_SOURCE 200809L
#include <strings.h>
#include "config.h"
#include "log.h"
#include "sway/commands.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/root.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "stringop.h"

static const char expected_syntax[] =
	"Expected 'swap container with id|con_id|mark <arg>'";

static void swap_places(struct sway_container *win1,
		struct sway_container *win2) {
	struct sway_container *temp = malloc(sizeof(struct sway_container));
	temp->pending.x = win1->pending.x;
	temp->pending.y = win1->pending.y;
	temp->pending.width = win1->pending.width;
	temp->pending.height = win1->pending.height;
	temp->width_fraction = win1->width_fraction;
	temp->height_fraction = win1->height_fraction;
	temp->pending.parent = win1->pending.parent;
	temp->pending.workspace = win1->pending.workspace;
	bool temp_floating = window_is_floating(win1);

	win1->pending.x = win2->pending.x;
	win1->pending.y = win2->pending.y;
	win1->pending.width = win2->pending.width;
	win1->pending.height = win2->pending.height;
	win1->width_fraction = win2->width_fraction;
	win1->height_fraction = win2->height_fraction;

	win2->pending.x = temp->pending.x;
	win2->pending.y = temp->pending.y;
	win2->pending.width = temp->pending.width;
	win2->pending.height = temp->pending.height;
	win2->width_fraction = temp->width_fraction;
	win2->height_fraction = temp->height_fraction;

	int temp_index = container_sibling_index(win1);
	if (win2->pending.parent) {
		column_insert_child(win2->pending.parent, win1,
				container_sibling_index(win2));
	} else if (window_is_floating(win2)) {
		workspace_add_floating(win2->pending.workspace, win1);
	} else {
		workspace_insert_tiling(win2->pending.workspace, win1,
				container_sibling_index(win2));
	}
	if (temp->pending.parent) {
		column_insert_child(temp->pending.parent, win2, temp_index);
	} else if (temp_floating) {
		workspace_add_floating(temp->pending.workspace, win2);
	} else {
		workspace_insert_tiling(temp->pending.workspace, win2, temp_index);
	}

	free(temp);
}

static void swap_focus(struct sway_container *win1,
		struct sway_container *win2, struct sway_seat *seat,
		struct sway_container *focus) {
	if (focus == win1 || focus == win2) {
		struct sway_workspace *ws1 = win1->pending.workspace;
		struct sway_workspace *ws2 = win2->pending.workspace;
		enum sway_container_layout layout1 = container_parent_layout(win1);
		enum sway_container_layout layout2 = container_parent_layout(win2);
		if (focus == win1 && (layout2 == L_TABBED || layout2 == L_STACKED)) {
			if (workspace_is_visible(ws2)) {
				seat_set_raw_focus(seat, &win2->node);
			}
			seat_set_focus_window(seat, ws1 != ws2 ? win2 : win1);
		} else if (focus == win2 && (layout1 == L_TABBED
					|| layout1 == L_STACKED)) {
			if (workspace_is_visible(ws1)) {
				seat_set_raw_focus(seat, &win1->node);
			}
			seat_set_focus_window(seat, ws1 != ws2 ? win1 : win2);
		} else if (ws1 != ws2) {
			seat_set_focus_window(seat, focus == win1 ? win2 : win1);
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

void container_swap(struct sway_container *win1, struct sway_container *win2) {
	if (!sway_assert(win1 && win2, "Cannot swap with nothing")) {
		return;
	}
	if (!sway_assert(container_is_window(win1), "Can only swap windows")) {
		return;
	}
	if (!sway_assert(container_is_window(win1), "Can only swap windows")) {
		return;
	}

	sway_log(SWAY_DEBUG, "Swapping containers %zu and %zu",
			win1->node.id, win2->node.id);

	enum sway_fullscreen_mode fs1 = win1->pending.fullscreen_mode;
	if (fs1) {
		container_fullscreen_disable(win1);
	}
	enum sway_fullscreen_mode fs2 = win2->pending.fullscreen_mode;
	if (fs2) {
		container_fullscreen_disable(win2);
	}

	struct sway_seat *seat = config->handler_context.seat;
	struct sway_container *focus = seat_get_focused_container(seat);
	struct sway_workspace *vis1 =
		output_get_active_workspace(win1->pending.workspace->output);
	struct sway_workspace *vis2 =
		output_get_active_workspace(win2->pending.workspace->output);
	if (!sway_assert(vis1 && vis2, "win1 or win2 are on an output without a"
				"workspace. This should not happen")) {
		return;
	}

	char *stored_prev_name = NULL;
	if (seat->prev_workspace_name) {
		stored_prev_name = strdup(seat->prev_workspace_name);
	}

	swap_places(win1, win2);

	if (!workspace_is_visible(vis1)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &vis1->node));
	}
	if (!workspace_is_visible(vis2)) {
		seat_set_focus(seat, seat_get_focus_inactive(seat, &vis2->node));
	}

	swap_focus(win1, win2, seat, focus);

	if (stored_prev_name) {
		free(seat->prev_workspace_name);
		seat->prev_workspace_name = stored_prev_name;
	}

	if (fs1) {
		container_set_fullscreen(win2, fs1);
	}
	if (fs2) {
		container_set_fullscreen(win1, fs2);
	}
}

static bool test_con_id(struct sway_container *container, void *data) {
	size_t *con_id = data;
	return container->node.id == *con_id;
}

#if HAVE_XWAYLAND
static bool test_id(struct sway_container *container, void *data) {
	xcb_window_t *wid = data;
	return (container->view && container->view->type == SWAY_VIEW_XWAYLAND
			&& container->view->wlr_xwayland_surface->window_id == *wid);
}
#endif

static bool test_mark(struct sway_container *container, void *mark) {
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

	struct sway_container *current = config->handler_context.window;
	struct sway_container *other = NULL;

	char *value = join_args(argv + 3, argc - 3);
	if (strcasecmp(argv[2], "id") == 0) {
#if HAVE_XWAYLAND
		xcb_window_t id = strtol(value, NULL, 0);
		// TODO (wmiiv) should be root_find_window.
		other = root_find_container(test_id, &id);
#endif
	} else if (strcasecmp(argv[2], "con_id") == 0) {
		size_t con_id = atoi(value);
		other = root_find_container(test_con_id, &con_id);
	} else if (strcasecmp(argv[2], "mark") == 0) {
		other = root_find_container(test_mark, value);
	} else {
		free(value);
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (!other || !container_is_window(other)) {
		error = cmd_results_new(CMD_FAILURE,
				"Failed to find %s '%s'", argv[2], value);
	} else if (!current) {
		error = cmd_results_new(CMD_FAILURE,
				"Can only swap with containers and views");
	} else if (current == other) {
		error = cmd_results_new(CMD_FAILURE,
				"Cannot swap a container with itself");
	} else if (container_has_ancestor(current, other)
			|| container_has_ancestor(other, current)) {
		error = cmd_results_new(CMD_FAILURE,
				"Cannot swap ancestor and descendant");
	}

	free(value);

	if (error) {
		return error;
	}

	container_swap(current, other);

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		struct sway_node *current_parent = node_get_parent(&current->node);
		struct sway_node *other_parent = node_get_parent(&other->node);
		if (current_parent) {
			arrange_node(current_parent);
		}
		if (other_parent && current_parent != other_parent) {
			arrange_node(other_parent);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
