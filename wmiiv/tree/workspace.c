#define _POSIX_C_SOURCE 200809
#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include "stringop.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/cursor.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/node.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct workspace_config *workspace_find_config(const char *workspace_name) {
	for (int i = 0; i < config->workspace_configs->length; ++i) {
		struct workspace_config *wsc = config->workspace_configs->items[i];
		if (strcmp(wsc->workspace, workspace_name) == 0) {
			return wsc;
		}
	}
	return NULL;
}

struct wmiiv_output *workspace_get_initial_output(const char *name) {
	// Check workspace configs for a workspace<->output pair
	struct workspace_config *wsc = workspace_find_config(name);
	if (wsc) {
		for (int i = 0; i < wsc->outputs->length; i++) {
			struct wmiiv_output *output =
				output_by_name_or_id(wsc->outputs->items[i]);
			if (output) {
				return output;
			}
		}
	}
	// Otherwise try to put it on the focused output
	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_node *focus = seat_get_focus_inactive(seat, &root->node);
	if (focus && focus->type == N_WORKSPACE) {
		return focus->wmiiv_workspace->output;
	} else if (focus && focus->type == N_WINDOW) {
		return focus->wmiiv_window->pending.workspace->output;
	}
	// Fallback to the first output or the headless output
	return root->outputs->length ? root->outputs->items[0] : root->fallback_output;
}

struct wmiiv_workspace *workspace_create(struct wmiiv_output *output,
		const char *name) {
	if (output == NULL) {
		output = workspace_get_initial_output(name);
	}

	wmiiv_log(WMIIV_DEBUG, "Adding workspace %s for output %s", name,
			output->wlr_output->name);

	struct wmiiv_workspace *workspace = calloc(1, sizeof(struct wmiiv_workspace));
	if (!workspace) {
		wmiiv_log(WMIIV_ERROR, "Unable to allocate wmiiv_workspace");
		return NULL;
	}
	node_init(&workspace->node, N_WORKSPACE, workspace);
	workspace->name = name ? strdup(name) : NULL;
	workspace->floating = create_list();
	workspace->tiling = create_list();
	workspace->output_priority = create_list();

	workspace->gaps_outer = config->gaps_outer;
	workspace->gaps_inner = config->gaps_inner;
	if (name) {
		struct workspace_config *wsc = workspace_find_config(name);
		if (wsc) {
			if (wsc->gaps_outer.top != INT_MIN) {
				workspace->gaps_outer.top = wsc->gaps_outer.top;
			}
			if (wsc->gaps_outer.right != INT_MIN) {
				workspace->gaps_outer.right = wsc->gaps_outer.right;
			}
			if (wsc->gaps_outer.bottom != INT_MIN) {
				workspace->gaps_outer.bottom = wsc->gaps_outer.bottom;
			}
			if (wsc->gaps_outer.left != INT_MIN) {
				workspace->gaps_outer.left = wsc->gaps_outer.left;
			}
			if (wsc->gaps_inner != INT_MIN) {
				workspace->gaps_inner = wsc->gaps_inner;
			}

			// Add output priorities
			for (int i = 0; i < wsc->outputs->length; ++i) {
				char *name = wsc->outputs->items[i];
				if (strcmp(name, "*") != 0) {
					list_add(workspace->output_priority, strdup(name));
				}
			}
		}
	}

	// If not already added, add the output to the lowest priority
	workspace_output_add_priority(workspace, output);

	output_add_workspace(output, workspace);
	output_sort_workspaces(output);

	ipc_event_workspace(NULL, workspace, "init");
	wl_signal_emit(&root->events.new_node, &workspace->node);

	return workspace;
}

void workspace_destroy(struct wmiiv_workspace *workspace) {
	if (!wmiiv_assert(workspace->node.destroying,
				"Tried to free workspace which wasn't marked as destroying")) {
		return;
	}
	if (!wmiiv_assert(workspace->node.ntxnrefs == 0, "Tried to free workspace "
				"which is still referenced by transactions")) {
		return;
	}

	free(workspace->name);
	free(workspace->representation);
	list_free_items_and_destroy(workspace->output_priority);
	list_free(workspace->floating);
	list_free(workspace->tiling);
	list_free(workspace->current.floating);
	list_free(workspace->current.tiling);
	free(workspace);
}

void workspace_begin_destroy(struct wmiiv_workspace *workspace) {
	wmiiv_log(WMIIV_DEBUG, "Destroying workspace '%s'", workspace->name);
	ipc_event_workspace(NULL, workspace, "empty"); // intentional
	wl_signal_emit(&workspace->node.events.destroy, &workspace->node);

	if (workspace->output) {
		workspace_detach(workspace);
	}
	workspace->node.destroying = true;
	node_set_dirty(&workspace->node);
}

void workspace_consider_destroy(struct wmiiv_workspace *workspace) {
	if (workspace->tiling->length || workspace->floating->length) {
		return;
	}

	if (workspace->output && output_get_active_workspace(workspace->output) == workspace) {
		return;
	}

	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct wmiiv_workspace *active_workspace = seat_get_focused_workspace(seat);
		if (workspace == active_workspace) {
			return;
		}
	}

	workspace_begin_destroy(workspace);
}

static bool workspace_valid_on_output(const char *output_name,
		const char *workspace_name) {
	struct workspace_config *wsc = workspace_find_config(workspace_name);
	char identifier[128];
	struct wmiiv_output *output = output_by_name_or_id(output_name);
	if (!output) {
		return false;
	}
	output_name = output->wlr_output->name;
	output_get_identifier(identifier, sizeof(identifier), output);

	if (!wsc) {
		return true;
	}

	for (int i = 0; i < wsc->outputs->length; i++) {
		if (strcmp(wsc->outputs->items[i], "*") == 0 ||
				strcmp(wsc->outputs->items[i], output_name) == 0 ||
				strcmp(wsc->outputs->items[i], identifier) == 0) {
			return true;
		}
	}

	return false;
}

static void workspace_name_from_binding(const struct wmiiv_binding * binding,
		const char* output_name, int *min_order, char **earliest_name) {
	char *cmdlist = strdup(binding->command);
	char *dup = cmdlist;
	char *name = NULL;

	// workspace n
	char *cmd = argsep(&cmdlist, " ", NULL);
	if (cmdlist) {
		name = argsep(&cmdlist, ",;", NULL);
	}

	// TODO: support "move container to workspace" bindings as well

	if (strcmp("workspace", cmd) == 0 && name) {
		char *_target = strdup(name);
		_target = do_var_replacement(_target);
		strip_quotes(_target);
		wmiiv_log(WMIIV_DEBUG, "Got valid workspace command for target: '%s'",
				_target);

		// Make sure that the command references an actual workspace
		// not a command about workspaces
		if (strcmp(_target, "next") == 0 ||
				strcmp(_target, "prev") == 0 ||
				strcmp(_target, "next_on_output") == 0 ||
				strcmp(_target, "prev_on_output") == 0 ||
				strcmp(_target, "number") == 0 ||
				strcmp(_target, "back_and_forth") == 0 ||
				strcmp(_target, "current") == 0) {
			free(_target);
			free(dup);
			return;
		}

		// If the command is workspace number <name>, isolate the name
		if (strncmp(_target, "number ", strlen("number ")) == 0) {
			size_t length = strlen(_target) - strlen("number ") + 1;
			char *temp = malloc(length);
			strncpy(temp, _target + strlen("number "), length - 1);
			temp[length - 1] = '\0';
			free(_target);
			_target = temp;
			wmiiv_log(WMIIV_DEBUG, "Isolated name from workspace number: '%s'", _target);

			// Make sure the workspace number doesn't already exist
			if (isdigit(_target[0]) && workspace_by_number(_target)) {
				free(_target);
				free(dup);
				return;
			}
		}

		// Make sure that the workspace doesn't already exist
		if (workspace_by_name(_target)) {
			free(_target);
			free(dup);
			return;
		}

		// make sure that the workspace can appear on the given
		// output
		if (!workspace_valid_on_output(output_name, _target)) {
			free(_target);
			free(dup);
			return;
		}

		if (binding->order < *min_order) {
			*min_order = binding->order;
			free(*earliest_name);
			*earliest_name = _target;
			wmiiv_log(WMIIV_DEBUG, "Workspace: Found free name %s", _target);
		} else {
			free(_target);
		}
	}
	free(dup);
}

char *workspace_next_name(const char *output_name) {
	wmiiv_log(WMIIV_DEBUG, "Workspace: Generating new workspace name for output %s",
			output_name);
	// Scan for available workspace names by looking through output-workspace
	// assignments primarily, falling back to bindings and numbers.
	struct wmiiv_mode *mode = config->current_mode;

	char identifier[128];
	struct wmiiv_output *output = output_by_name_or_id(output_name);
	if (!output) {
		return NULL;
	}
	output_name = output->wlr_output->name;
	output_get_identifier(identifier, sizeof(identifier), output);

	int order = INT_MAX;
	char *target = NULL;
	for (int i = 0; i < mode->keysym_bindings->length; ++i) {
		workspace_name_from_binding(mode->keysym_bindings->items[i],
				output_name, &order, &target);
	}
	for (int i = 0; i < mode->keycode_bindings->length; ++i) {
		workspace_name_from_binding(mode->keycode_bindings->items[i],
				output_name, &order, &target);
	}
	for (int i = 0; i < config->workspace_configs->length; ++i) {
		// Unlike with bindings, this does not guarantee order
		const struct workspace_config *wsc = config->workspace_configs->items[i];
		if (workspace_by_name(wsc->workspace)) {
			continue;
		}
		bool found = false;
		for (int j = 0; j < wsc->outputs->length; ++j) {
			if (strcmp(wsc->outputs->items[j], "*") == 0 ||
					strcmp(wsc->outputs->items[j], output_name) == 0 ||
					strcmp(wsc->outputs->items[j], identifier) == 0) {
				found = true;
				free(target);
				target = strdup(wsc->workspace);
				break;
			}
		}
		if (found) {
			break;
		}
	}
	if (target != NULL) {
		return target;
	}
	// As a fall back, use the next available number
	char name[12] = "";
	unsigned int workspace_num = 1;
	do {
		snprintf(name, sizeof(name), "%u", workspace_num++);
	} while (workspace_by_number(name));
	return strdup(name);
}

static bool _workspace_by_number(struct wmiiv_workspace *workspace, void *data) {
	char *name = data;
	char *workspace_name = workspace->name;
	while (isdigit(*name)) {
		if (*name++ != *workspace_name++) {
			return false;
		}
	}
	return !isdigit(*workspace_name);
}

struct wmiiv_workspace *workspace_by_number(const char* name) {
	return root_find_workspace(_workspace_by_number, (void *) name);
}

static bool _workspace_by_name(struct wmiiv_workspace *workspace, void *data) {
	return strcasecmp(workspace->name, data) == 0;
}

struct wmiiv_workspace *workspace_by_name(const char *name) {
	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_workspace *current = seat_get_focused_workspace(seat);

	if (current && strcmp(name, "prev") == 0) {
		return workspace_prev(current);
	} else if (current && strcmp(name, "prev_on_output") == 0) {
		return workspace_output_prev(current);
	} else if (current && strcmp(name, "next") == 0) {
		return workspace_next(current);
	} else if (current && strcmp(name, "next_on_output") == 0) {
		return workspace_output_next(current);
	} else if (strcmp(name, "current") == 0) {
		return current;
	} else if (strcasecmp(name, "back_and_forth") == 0) {
		struct wmiiv_seat *seat = input_manager_current_seat();
		if (!seat->prev_workspace_name) {
			return NULL;
		}
		return root_find_workspace(_workspace_by_name,
				(void*)seat->prev_workspace_name);
	} else {
		return root_find_workspace(_workspace_by_name, (void*)name);
	}
}

static int workspace_get_number(struct wmiiv_workspace *workspace) {
	char *endptr = NULL;
	errno = 0;
	long long n = strtoll(workspace->name, &endptr, 10);
	if (errno != 0 || n > INT32_MAX || n < 0 || endptr == workspace->name) {
		n = -1;
	}
	return n;
}

struct wmiiv_workspace *workspace_prev(struct wmiiv_workspace *workspace) {
	int n = workspace_get_number(workspace);
	struct wmiiv_workspace *prev = NULL, *last = NULL, *other = NULL;
	bool found = false;
	if (n < 0) {
		// Find the prev named workspace
		int othern = -1;
		for (int i = root->outputs->length - 1; i >= 0; i--) {
			struct wmiiv_output *output = root->outputs->items[i];
			for (int j = output->workspaces->length - 1; j >= 0; j--) {
				struct wmiiv_workspace *candidate = output->workspaces->items[j];
				int wsn = workspace_get_number(candidate);
				if (!last) {
					// The first workspace in reverse order
					last = candidate;
				}
				if (!other || (wsn >= 0 && wsn > othern)) {
					// The last (greatest) numbered workspace.
					other = candidate;
					othern = workspace_get_number(other);
				}
				if (candidate== workspace) {
					found = true;
				} else if (wsn < 0 && found) {
					// Found a non-numbered workspace before current
					return candidate;
				}
			}
		}
	} else {
		// Find the prev numbered workspace
		int prevn = -1, lastn = -1;
		for (int i = root->outputs->length - 1; i >= 0; i--) {
			struct wmiiv_output *output = root->outputs->items[i];
			for (int j = output->workspaces->length - 1; j >= 0; j--) {
				struct wmiiv_workspace *candidate = output->workspaces->items[j];
				int wsn = workspace_get_number(candidate);
				if (!last || (wsn >= 0 && wsn > lastn)) {
					// The greatest numbered (or last) workspace
					last = candidate;
					lastn = workspace_get_number(last);
				}
				if (!other && wsn < 0) {
					// The last named workspace
					other = candidate;
				}
				if (wsn < 0) {
					// Haven't reached the numbered workspaces
					continue;
				}
				if (wsn < n && (!prev || wsn > prevn)) {
					// The closest workspace before the current
					prev = candidate;
					prevn = workspace_get_number(prev);
				}
			}
		}
	}

	if (!prev) {
		prev = other ? other : last;
	}
	return prev;
}

struct wmiiv_workspace *workspace_next(struct wmiiv_workspace *workspace) {
	int n = workspace_get_number(workspace);
	struct wmiiv_workspace *next = NULL, *first = NULL, *other = NULL;
	bool found = false;
	if (n < 0) {
		// Find the next named workspace
		int othern = -1;
		for (int i = 0; i < root->outputs->length; i++) {
			struct wmiiv_output *output = root->outputs->items[i];
			for (int j = 0; j < output->workspaces->length; j++) {
				struct wmiiv_workspace *candidate = output->workspaces->items[j];
				int wsn = workspace_get_number(candidate);
				if (!first) {
					// The first named workspace
					first = candidate;
				}
				if (!other || (wsn >= 0 && wsn < othern)) {
					// The first (least) numbered workspace
					other = candidate;
					othern = workspace_get_number(other);
				}
				if (candidate == workspace) {
					found = true;
				} else if (wsn < 0 && found) {
					// The first non-numbered workspace after the current
					return candidate;
				}
			}
		}
	} else {
		// Find the next numbered workspace
		int nextn = -1, firstn = -1;
		for (int i = 0; i < root->outputs->length; i++) {
			struct wmiiv_output *output = root->outputs->items[i];
			for (int j = 0; j < output->workspaces->length; j++) {
				struct wmiiv_workspace *candidate = output->workspaces->items[j];
				int wsn = workspace_get_number(candidate);
				if (!first || (wsn >= 0 && wsn < firstn)) {
					// The first (or least numbered) workspace
					first = candidate;
					firstn = workspace_get_number(first);
				}
				if (!other && wsn < 0) {
					// The first non-numbered workspace
					other = candidate;
				}
				if (wsn < 0) {
					// Checked all the numbered workspaces
					break;
				}
				if (n < wsn && (!next || wsn < nextn)) {
					// The first workspace numerically after the current
					next = candidate;
					nextn = workspace_get_number(next);
				}
			}
		}
	}

	if (!next) {
		// If there is no next workspace from the same category, return the
		// first from this category.
		next = other ? other : first;
	}
	return next;
}

/**
 * Get the previous or next workspace on the specified output. Wraps around at
 * the end and beginning.  If next is false, the previous workspace is returned,
 * otherwise the next one is returned.
 */
static struct wmiiv_workspace *workspace_output_prev_next_impl(
		struct wmiiv_output *output, int dir) {
	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_workspace *workspace = seat_get_focused_workspace(seat);
	if (!workspace) {
		wmiiv_log(WMIIV_DEBUG,
				"No focused workspace to base prev/next on output off of");
		return NULL;
	}

	int index = list_find(output->workspaces, workspace);
	size_t new_index = wrap(index + dir, output->workspaces->length);
	return output->workspaces->items[new_index];
}


struct wmiiv_workspace *workspace_output_next(struct wmiiv_workspace *current) {
	return workspace_output_prev_next_impl(current->output, 1);
}

struct wmiiv_workspace *workspace_output_prev(struct wmiiv_workspace *current) {
	return workspace_output_prev_next_impl(current->output, -1);
}

struct wmiiv_workspace *workspace_auto_back_and_forth(
		struct wmiiv_workspace *workspace) {
	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_workspace *active_workspace = NULL;
	struct wmiiv_node *focus = seat_get_focus_inactive(seat, &root->node);
	if (focus && focus->type == N_WORKSPACE) {
		active_workspace = focus->wmiiv_workspace;
	} else if (focus && (focus->type == N_WINDOW)) {
		active_workspace = focus->wmiiv_window->pending.workspace;
	}

	if (config->auto_back_and_forth && active_workspace && active_workspace == workspace &&
			seat->prev_workspace_name) {
		struct wmiiv_workspace *new_workspace =
			workspace_by_name(seat->prev_workspace_name);
		workspace = new_workspace ?
			new_workspace :
			workspace_create(NULL, seat->prev_workspace_name);
	}
	return workspace;
}

bool workspace_switch(struct wmiiv_workspace *workspace) {
	struct wmiiv_seat *seat = input_manager_current_seat();

	wmiiv_log(WMIIV_DEBUG, "Switching to workspace %p:%s",
		workspace, workspace->name);

	struct wmiiv_window *active_window = seat_get_active_window_for_workspace(seat, workspace);
	if (active_window) {
		seat_set_focus_window(seat, active_window);
	} else {
		seat_set_focus_workspace(seat, workspace);
	}
	return true;
}

bool workspace_is_visible(struct wmiiv_workspace *workspace) {
	if (workspace->node.destroying) {
		return false;
	}
	return output_get_active_workspace(workspace->output) == workspace;
}

bool workspace_is_empty(struct wmiiv_workspace *workspace) {
	if (workspace->tiling->length) {
		return false;
	}
	// Sticky views are not considered to be part of this workspace
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct wmiiv_window *floater = workspace->floating->items[i];
		if (!window_is_sticky(floater)) {
			return false;
		}
	}
	return true;
}

static int find_output(const void *id1, const void *id2) {
	return strcmp(id1, id2);
}

static int workspace_output_get_priority(struct wmiiv_workspace *workspace,
		struct wmiiv_output *output) {
	char identifier[128];
	output_get_identifier(identifier, sizeof(identifier), output);
	int index_id = list_seq_find(workspace->output_priority, find_output, identifier);
	int index_name = list_seq_find(workspace->output_priority, find_output,
			output->wlr_output->name);
	return index_name < 0 || index_id < index_name ? index_id : index_name;
}

void workspace_output_raise_priority(struct wmiiv_workspace *workspace,
		struct wmiiv_output *old_output, struct wmiiv_output *output) {
	int old_index = workspace_output_get_priority(workspace, old_output);
	if (old_index < 0) {
		return;
	}

	int new_index = workspace_output_get_priority(workspace, output);
	if (new_index < 0) {
		char identifier[128];
		output_get_identifier(identifier, sizeof(identifier), output);
		list_insert(workspace->output_priority, old_index, strdup(identifier));
	} else if (new_index > old_index) {
		char *name = workspace->output_priority->items[new_index];
		list_del(workspace->output_priority, new_index);
		list_insert(workspace->output_priority, old_index, name);
	}
}

void workspace_output_add_priority(struct wmiiv_workspace *workspace,
		struct wmiiv_output *output) {
	if (workspace_output_get_priority(workspace, output) < 0) {
		char identifier[128];
		output_get_identifier(identifier, sizeof(identifier), output);
		list_add(workspace->output_priority, strdup(identifier));
	}
}

struct wmiiv_output *workspace_output_get_highest_available(
		struct wmiiv_workspace *workspace, struct wmiiv_output *exclude) {
	char exclude_id[128] = {'\0'};
	if (exclude) {
		output_get_identifier(exclude_id, sizeof(exclude_id), exclude);
	}

	for (int i = 0; i < workspace->output_priority->length; i++) {
		char *name = workspace->output_priority->items[i];
		if (exclude && (strcmp(name, exclude->wlr_output->name) == 0
					|| strcmp(name, exclude_id) == 0)) {
			continue;
		}

		struct wmiiv_output *output = output_by_name_or_id(name);
		if (output) {
			return output;
		}
	}

	return NULL;
}

static bool find_urgent_iterator(struct wmiiv_window *container, void *data) {
	return container->view && view_is_urgent(container->view);
}

void workspace_detect_urgent(struct wmiiv_workspace *workspace) {
	bool new_urgent = (bool)workspace_find_window(workspace,
			find_urgent_iterator, NULL);

	if (workspace->urgent != new_urgent) {
		workspace->urgent = new_urgent;
		ipc_event_workspace(NULL, workspace, "urgent");
		output_damage_whole(workspace->output);
	}
}

void workspace_for_each_window(struct wmiiv_workspace *workspace, void (*f)(struct wmiiv_window *window, void *data), void *data) {
	// Tiling
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct wmiiv_column *column = workspace->tiling->items[i];
		column_for_each_child(column, f, data);
	}
	// Floating
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct wmiiv_window *window = workspace->floating->items[i];
		f(window, data);
	}
}

void workspace_for_each_column(struct wmiiv_workspace *workspace, void (*f)(struct wmiiv_column *column, void *data), void *data) {
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct wmiiv_column *column = workspace->tiling->items[i];
		f(column, data);
	}
}

struct wmiiv_window *workspace_find_window(struct wmiiv_workspace *workspace,
		bool (*test)(struct wmiiv_window *window, void *data), void *data) {
	struct wmiiv_window *result = NULL;
	// Tiling
	for (int i = 0; i < workspace->tiling->length; ++i) {
		struct wmiiv_column *child = workspace->tiling->items[i];
		if ((result = column_find_child(child, test, data))) {
			return result;
		}
	}
	// Floating
	for (int i = 0; i < workspace->floating->length; ++i) {
		struct wmiiv_window *child = workspace->floating->items[i];
		if (test(child, data)) {
			return child;
		}
	}
	return NULL;
}

static void set_workspace(struct wmiiv_window *container, void *data) {
	container->pending.workspace = container->pending.parent->pending.workspace;
}

void workspace_detach(struct wmiiv_workspace *workspace) {
	struct wmiiv_output *output = workspace->output;
	int index = list_find(output->workspaces, workspace);
	if (index != -1) {
		list_del(output->workspaces, index);
	}
	workspace->output = NULL;

	node_set_dirty(&workspace->node);
	node_set_dirty(&output->node);
}

struct wmiiv_column *workspace_add_tiling(struct wmiiv_workspace *workspace,
		struct wmiiv_column *column) {
	if (column->pending.workspace) {
		column_detach(column);
	}

	list_add(workspace->tiling, column);
	column->pending.workspace = workspace;

	column_for_each_child(column, set_workspace, NULL);
	workspace_update_representation(workspace);
	node_set_dirty(&workspace->node);
	node_set_dirty(&column->node);
	return column;
}

void workspace_add_floating(struct wmiiv_workspace *workspace,
		struct wmiiv_window *window) {
	if (window->pending.workspace) {
		window_detach(window);
	}

	list_add(workspace->floating, window);
	window->pending.workspace = workspace;

	window_handle_fullscreen_reparent(window);

	node_set_dirty(&workspace->node);
	node_set_dirty(&window->node);
}

void workspace_insert_tiling_direct(struct wmiiv_workspace *workspace,
		struct wmiiv_column *column, int index) {
	list_insert(workspace->tiling, index, column);
	column->pending.workspace = workspace;
	column_for_each_child(column, set_workspace, NULL);
	workspace_update_representation(workspace);
	node_set_dirty(&workspace->node);
	node_set_dirty(&column->node);
}

struct wmiiv_column *workspace_insert_tiling(struct wmiiv_workspace *workspace,
		struct wmiiv_column *column, int index) {
	if (column->pending.workspace) {
		column_detach(column);
	}

	workspace_insert_tiling_direct(workspace, column, index);
	return column;
}

bool workspace_has_single_visible_container(struct wmiiv_workspace *workspace) {
	struct wmiiv_seat *seat = input_manager_get_default_seat();
	struct wmiiv_window *focus =
		seat_get_focus_inactive_tiling(seat, workspace);
	if (focus && !focus->view) {
		focus = seat_get_focus_inactive_view(seat, &focus->node);
	}
	return (focus && focus->view && view_ancestor_is_only_visible(focus->view));
}

void workspace_add_gaps(struct wmiiv_workspace *workspace) {
	if (config->smart_gaps == SMART_GAPS_ON
			&& workspace_has_single_visible_container(workspace)) {
		workspace->current_gaps.top = 0;
		workspace->current_gaps.right = 0;
		workspace->current_gaps.bottom = 0;
		workspace->current_gaps.left = 0;
		return;
	}

	if (config->smart_gaps == SMART_GAPS_INVERSE_OUTER
			&& !workspace_has_single_visible_container(workspace)) {
		workspace->current_gaps.top = 0;
		workspace->current_gaps.right = 0;
		workspace->current_gaps.bottom = 0;
		workspace->current_gaps.left = 0;
	} else {
		workspace->current_gaps = workspace->gaps_outer;
	}

	// Add inner gaps and make sure we don't turn out negative
	workspace->current_gaps.top = fmax(0, workspace->current_gaps.top + workspace->gaps_inner);
	workspace->current_gaps.right = fmax(0, workspace->current_gaps.right + workspace->gaps_inner);
	workspace->current_gaps.bottom = fmax(0, workspace->current_gaps.bottom + workspace->gaps_inner);
	workspace->current_gaps.left = fmax(0, workspace->current_gaps.left + workspace->gaps_inner);

	// Now that we have the total gaps calculated we may need to clamp them in
	// case they've made the available area too small
	if (workspace->width - workspace->current_gaps.left - workspace->current_gaps.right < MIN_SANE_W
			&& workspace->current_gaps.left + workspace->current_gaps.right > 0) {
		int total_gap = fmax(0, workspace->width - MIN_SANE_W);
		double left_gap_frac = ((double)workspace->current_gaps.left /
			((double)workspace->current_gaps.left + (double)workspace->current_gaps.right));
		workspace->current_gaps.left = left_gap_frac * total_gap;
		workspace->current_gaps.right = total_gap - workspace->current_gaps.left;
	}
	if (workspace->height - workspace->current_gaps.top - workspace->current_gaps.bottom < MIN_SANE_H
			&& workspace->current_gaps.top + workspace->current_gaps.bottom > 0) {
		int total_gap = fmax(0, workspace->height - MIN_SANE_H);
		double top_gap_frac = ((double) workspace->current_gaps.top /
			((double)workspace->current_gaps.top + (double)workspace->current_gaps.bottom));
		workspace->current_gaps.top = top_gap_frac * total_gap;
		workspace->current_gaps.bottom = total_gap - workspace->current_gaps.top;
	}

	workspace->x += workspace->current_gaps.left;
	workspace->y += workspace->current_gaps.top;
	workspace->width -= workspace->current_gaps.left + workspace->current_gaps.right;
	workspace->height -= workspace->current_gaps.top + workspace->current_gaps.bottom;
}

struct wmiiv_window *workspace_split(struct wmiiv_workspace *workspace,
		enum wmiiv_window_layout layout) {
	wmiiv_assert(false, "workspace_split is deprecated");

	return NULL;
}

static size_t workspace_build_representation(struct wmiiv_workspace *workspace, char *buffer) {
	list_t *children = workspace->tiling;

	size_t len = 2;
	lenient_strcat(buffer, "H[");

	for (int i = 0; i < children->length; ++i) {
		if (i != 0) {
			++len;
			lenient_strcat(buffer, " ");
		}
		struct wmiiv_window *child = children->items[i];
		const char *identifier = NULL;
		identifier = child->formatted_title;
		if (identifier) {
			len += strlen(identifier);
			lenient_strcat(buffer, identifier);
		} else {
			len += 6;
			lenient_strcat(buffer, "(null)");
		}
	}
	++len;
	lenient_strcat(buffer, "]");
	return len;
}
void workspace_update_representation(struct wmiiv_workspace *workspace) {
	size_t len = workspace_build_representation(workspace, NULL);
	free(workspace->representation);
	workspace->representation = calloc(len + 1, sizeof(char));
	if (!wmiiv_assert(workspace->representation, "Unable to allocate title string")) {
		return;
	}
	workspace_build_representation(workspace, workspace->representation);
}

void workspace_get_box(struct wmiiv_workspace *workspace, struct wlr_box *box) {
	box->x = workspace->x;
	box->y = workspace->y;
	box->width = workspace->width;
	box->height = workspace->height;
}

static void count_tiling_views(struct wmiiv_window *container, void *data) {
	if (!window_is_floating(container)) {
		size_t *count = data;
		*count += 1;
	}
}

size_t workspace_num_tiling_views(struct wmiiv_workspace *workspace) {
	size_t count = 0;
	workspace_for_each_window(workspace, count_tiling_views, &count);
	return count;
}

static void count_sticky_containers(struct wmiiv_window *container, void *data) {
	if (!window_is_sticky(container)) {
		return;
	}

	size_t *count = data;
	*count += 1;
}

size_t workspace_num_sticky_containers(struct wmiiv_workspace *workspace) {
	size_t count = 0;
	workspace_for_each_window(workspace, count_sticky_containers, &count);
	return count;
}
