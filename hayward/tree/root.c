#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output_layout.h>
#include "hayward/desktop/transaction.h"
#include "hayward/input/seat.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/window.h"
#include "hayward/tree/root.h"
#include "hayward/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "util.h"

struct hayward_root *root;

static void output_layout_handle_change(struct wl_listener *listener,
		void *data) {
	arrange_root();
	transaction_commit_dirty();
}

struct hayward_root *root_create(void) {
	struct hayward_root *root = calloc(1, sizeof(struct hayward_root));
	if (!root) {
		hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_root");
		return NULL;
	}
	node_init(&root->node, N_ROOT, root);
	root->output_layout = wlr_output_layout_create();
	wl_list_init(&root->all_outputs);
#if HAVE_XWAYLAND
	wl_list_init(&root->xwayland_unmanaged);
#endif
	wl_list_init(&root->drag_icons);
	wl_signal_init(&root->events.new_node);
	root->outputs = create_list();

	root->output_layout_change.notify = output_layout_handle_change;
	wl_signal_add(&root->output_layout->events.change,
		&root->output_layout_change);
	return root;
}

void root_destroy(struct hayward_root *root) {
	wl_list_remove(&root->output_layout_change.link);
	list_free(root->outputs);
	wlr_output_layout_destroy(root->output_layout);
	free(root);
}

struct pid_workspace {
	pid_t pid;
	char *workspace;
	struct timespec time_added;

	struct hayward_output *output;
	struct wl_listener output_destroy;

	struct wl_list link;
};

static struct wl_list pid_workspaces;

/**
 * Get the pid of a parent process given the pid of a child process.
 *
 * Returns the parent pid or NULL if the parent pid cannot be determined.
 */
static pid_t get_parent_pid(pid_t child) {
	pid_t parent = -1;
	char file_name[100];
	char *buffer = NULL;
	const char *sep = " ";
	FILE *stat = NULL;
	size_t buf_size = 0;

	snprintf(file_name, sizeof(file_name), "/proc/%d/stat", child);

	if ((stat = fopen(file_name, "r"))) {
		if (getline(&buffer, &buf_size, stat) != -1) {
			strtok(buffer, sep); // pid
			strtok(NULL, sep);   // executable name
			strtok(NULL, sep);   // state
			char *token = strtok(NULL, sep);   // parent pid
			parent = strtol(token, NULL, 10);
		}
		free(buffer);
		fclose(stat);
	}

	if (parent) {
		return (parent == child) ? -1 : parent;
	}

	return -1;
}

static void pid_workspace_destroy(struct pid_workspace *pw) {
	wl_list_remove(&pw->output_destroy.link);
	wl_list_remove(&pw->link);
	free(pw->workspace);
	free(pw);
}

struct hayward_workspace *root_workspace_for_pid(pid_t pid) {
	if (!pid_workspaces.prev && !pid_workspaces.next) {
		wl_list_init(&pid_workspaces);
		return NULL;
	}

	struct hayward_workspace *workspace = NULL;
	struct pid_workspace *pw = NULL;

	hayward_log(HAYWARD_DEBUG, "Looking up workspace for pid %d", pid);

	do {
		struct pid_workspace *_pw = NULL;
		wl_list_for_each(_pw, &pid_workspaces, link) {
			if (pid == _pw->pid) {
				pw = _pw;
				hayward_log(HAYWARD_DEBUG,
						"found pid_workspace for pid %d, workspace %s",
						pid, pw->workspace);
				goto found;
			}
		}
		pid = get_parent_pid(pid);
	} while (pid > 1);
found:

	if (pw && pw->workspace) {
		workspace = workspace_by_name(pw->workspace);

		if (!workspace) {
			hayward_log(HAYWARD_DEBUG,
					"Creating workspace %s for pid %d because it disappeared",
					pw->workspace, pid);

			struct hayward_output *output = pw->output;
			if (pw->output && !pw->output->enabled) {
				hayward_log(HAYWARD_DEBUG,
						"Workspace output %s is disabled, trying another one",
						pw->output->wlr_output->name);
				output = NULL;
			}

			workspace = workspace_create(output, pw->workspace);
		}

		pid_workspace_destroy(pw);
	}

	return workspace;
}

static void pw_handle_output_destroy(struct wl_listener *listener, void *data) {
	struct pid_workspace *pw = wl_container_of(listener, pw, output_destroy);
	pw->output = NULL;
	wl_list_remove(&pw->output_destroy.link);
	wl_list_init(&pw->output_destroy.link);
}

void root_record_workspace_pid(pid_t pid) {
	hayward_log(HAYWARD_DEBUG, "Recording workspace for process %d", pid);
	if (!pid_workspaces.prev && !pid_workspaces.next) {
		wl_list_init(&pid_workspaces);
	}

	struct hayward_seat *seat = input_manager_current_seat();
	struct hayward_workspace *workspace = seat_get_focused_workspace(seat);
	if (!workspace) {
		hayward_log(HAYWARD_DEBUG, "Bailing out, no workspace");
		return;
	}
	struct hayward_output *output = workspace->pending.output;
	if (!output) {
		hayward_log(HAYWARD_DEBUG, "Bailing out, no output");
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	// Remove expired entries
	static const int timeout = 60;
	struct pid_workspace *old, *_old;
	wl_list_for_each_safe(old, _old, &pid_workspaces, link) {
		if (now.tv_sec - old->time_added.tv_sec >= timeout) {
			pid_workspace_destroy(old);
		}
	}

	struct pid_workspace *pw = calloc(1, sizeof(struct pid_workspace));
	pw->workspace = strdup(workspace->name);
	pw->output = output;
	pw->pid = pid;
	memcpy(&pw->time_added, &now, sizeof(struct timespec));
	pw->output_destroy.notify = pw_handle_output_destroy;
	wl_signal_add(&output->wlr_output->events.destroy, &pw->output_destroy);
	wl_list_insert(&pid_workspaces, &pw->link);
}

void root_remove_workspace_pid(pid_t pid) {
	if (!pid_workspaces.prev || !pid_workspaces.next) {
		return;
	}

	struct pid_workspace *pw, *tmp;
	wl_list_for_each_safe(pw, tmp, &pid_workspaces, link) {
		if (pid == pw->pid) {
			pid_workspace_destroy(pw);
			return;
		}
	}
}

void root_for_each_workspace(void (*f)(struct hayward_workspace *workspace, void *data),
		void *data) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		output_for_each_workspace(output, f, data);
	}
}

void root_for_each_window(void (*f)(struct hayward_window *window, void *data),
		void *data) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		output_for_each_window(output, f, data);
	}

	// Saved workspaces
	for (int i = 0; i < root->fallback_output->pending.workspaces->length; ++i) {
		struct hayward_workspace *workspace = root->fallback_output->pending.workspaces->items[i];
		workspace_for_each_window(workspace, f, data);
	}
}

struct hayward_output *root_find_output(
		bool (*test)(struct hayward_output *output, void *data), void *data) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		if (test(output, data)) {
			return output;
		}
	}
	return NULL;
}

struct hayward_workspace *root_find_workspace(
		bool (*test)(struct hayward_workspace *workspace, void *data), void *data) {
	struct hayward_workspace *result = NULL;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		if ((result = output_find_workspace(output, test, data))) {
			return result;
		}
	}
	return NULL;
}

struct hayward_window *root_find_window(
		bool (*test)(struct hayward_window *window, void *data), void *data) {
	struct hayward_window *result = NULL;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		if ((result = output_find_window(output, test, data))) {
			return result;
		}
	}

	// Saved workspaces
	for (int i = 0; i < root->fallback_output->pending.workspaces->length; ++i) {
		struct hayward_workspace *workspace = root->fallback_output->pending.workspaces->items[i];
		if ((result = workspace_find_window(workspace, test, data))) {
			return result;
		}
	}

	return NULL;
}

void root_get_box(struct hayward_root *root, struct wlr_box *box) {
	box->x = root->x;
	box->y = root->y;
	box->width = root->width;
	box->height = root->height;
}

void root_rename_pid_workspaces(const char *old_name, const char *new_name) {
	if (!pid_workspaces.prev && !pid_workspaces.next) {
		wl_list_init(&pid_workspaces);
	}

	struct pid_workspace *pw = NULL;
	wl_list_for_each(pw, &pid_workspaces, link) {
		if (strcmp(pw->workspace, old_name) == 0) {
			free(pw->workspace);
			pw->workspace = strdup(new_name);
		}
	}
}