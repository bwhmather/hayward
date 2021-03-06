#ifndef _HAYWARD_ROOT_H
#define _HAYWARD_ROOT_H
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/render/wlr_texture.h>
#include "hayward/tree/window.h"
#include "hayward/tree/node.h"
#include "config.h"
#include "list.h"
#include <ctype.h>

extern struct hayward_root *root;

struct hayward_root_state {
	list_t *workspaces;
	struct hayward_workspace *active_workspace;
	struct hayward_output *active_output;
};

struct hayward_root {
	struct hayward_node node;
	struct wlr_output_layout *output_layout;

	struct wl_listener output_layout_change;
#if HAVE_XWAYLAND
	struct wl_list xwayland_unmanaged; // hayward_xwayland_unmanaged::link
#endif
	struct wl_list drag_icons; // hayward_drag_icon::link

	// Includes disabled outputs
	struct wl_list all_outputs; // hayward_output::link

	double x, y;
	double width, height;

	list_t *outputs; // struct hayward_output

	// For when there's no connected outputs
	struct hayward_output *fallback_output;

	struct hayward_root_state current;
	struct hayward_root_state pending;

	/**
	 * The nodes that are currently receiving input events.
	 * These are tracked as we need to notify listeners over IPC when they
	 * change, as well as do stuff like clearing the urgent flag.
	 * We don't use the `current` state because this takes some time to
	 * propagate through the transaction system.
	 */
	struct hayward_window *focused_window;
	struct hayward_workspace *focused_workspace;

	struct {
		struct wl_signal new_node;
	} events;
};

struct hayward_root *root_create(void);

void root_destroy(struct hayward_root *root);

struct hayward_workspace *root_workspace_for_pid(pid_t pid);

void root_record_workspace_pid(pid_t pid);

void root_remove_workspace_pid(pid_t pid);

struct hayward_output *root_find_output(
		bool (*test)(struct hayward_output *output, void *data), void *data);

void root_get_box(struct hayward_root *root, struct wlr_box *box);

void root_rename_pid_workspaces(const char *old_name, const char *new_name);

void root_add_workspace(struct hayward_workspace *workspace);
void root_remove_workspace(struct hayward_workspace *workspace);

void root_sort_workspaces(void);

void root_set_active_workspace(struct hayward_workspace *workspace);
struct hayward_workspace *root_get_active_workspace(void);
struct hayward_workspace *root_get_current_active_workspace(void);

void root_set_active_output(struct hayward_output *output);
struct hayward_output *root_get_active_output(void);
struct hayward_output *root_get_current_active_output(void);

/**
 * Helper functions that traverse the tree to focus the right window.
 */
void root_set_focused_window(struct hayward_window *window);
struct hayward_window *root_get_focused_window(void);

void root_commit_focus(void);

void root_for_each_workspace(void (*f)(struct hayward_workspace *workspace, void *data), void *data);

void root_for_each_window(void (*f)(struct hayward_window *window, void *data), void *data);

struct hayward_workspace *root_find_workspace(bool (*test)(struct hayward_workspace *workspace, void *data), void *data);

struct hayward_window *root_find_window(bool (*test)(struct hayward_window *window, void *data), void *data);

#endif
