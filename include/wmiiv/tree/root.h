#ifndef _WMIIV_ROOT_H
#define _WMIIV_ROOT_H
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/render/wlr_texture.h>
#include "wmiiv/tree/window.h"
#include "wmiiv/tree/node.h"
#include "config.h"
#include "list.h"

extern struct wmiiv_root *root;

struct wmiiv_root {
	struct wmiiv_node node;
	struct wlr_output_layout *output_layout;

	struct wl_listener output_layout_change;
#if HAVE_XWAYLAND
	struct wl_list xwayland_unmanaged; // wmiiv_xwayland_unmanaged::link
#endif
	struct wl_list drag_icons; // wmiiv_drag_icon::link

	// Includes disabled outputs
	struct wl_list all_outputs; // wmiiv_output::link

	double x, y;
	double width, height;

	list_t *outputs; // struct wmiiv_output

	// For when there's no connected outputs
	struct wmiiv_output *fallback_output;

	struct wmiiv_window *fullscreen_global;

	struct {
		struct wl_signal new_node;
	} events;
};

struct wmiiv_root *root_create(void);

void root_destroy(struct wmiiv_root *root);

struct wmiiv_workspace *root_workspace_for_pid(pid_t pid);

void root_record_workspace_pid(pid_t pid);

void root_remove_workspace_pid(pid_t pid);

void root_for_each_workspace(void (*f)(struct wmiiv_workspace *workspace, void *data),
		void *data);

void root_for_each_window(void (*f)(struct wmiiv_window *window, void *data),
		void *data);

struct wmiiv_output *root_find_output(
		bool (*test)(struct wmiiv_output *output, void *data), void *data);

struct wmiiv_workspace *root_find_workspace(
		bool (*test)(struct wmiiv_workspace *workspace, void *data), void *data);

struct wmiiv_window *root_find_window(
		bool (*test)(struct wmiiv_window *window, void *data), void *data);

void root_get_box(struct wmiiv_root *root, struct wlr_box *box);

void root_rename_pid_workspaces(const char *old_name, const char *new_name);

#endif
