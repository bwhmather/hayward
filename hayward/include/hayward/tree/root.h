#ifndef _HAYWARD_ROOT_H
#define _HAYWARD_ROOT_H

#include <stdbool.h>
#include <sys/types.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>

#include <hayward/tree/node.h>
#include <hayward/tree/window.h>

#include <config.h>

extern struct hayward_root *root;

struct hayward_root_state {
    list_t *workspaces;

    /**
     * An optional explicitly focused surface.   Will only be used if there
     * is no active window or layer set.
     */
    struct wlr_surface *focused_surface;

    struct hayward_workspace *active_workspace;
    struct hayward_output *active_output;

    /**
     * An optional layer (top/bottom/side bar) that should receive input
     * events.  If set, will take priority over any active window or
     * explicitly focused surface.
     */
    struct wlr_layer_surface_v1 *focused_layer;
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

    struct hayward_root_state pending;
    struct hayward_root_state committed;
    struct hayward_root_state current;

    /**
     * The nodes that are currently actually receiving input events.  These
     * are distinct from the state in the `current` struct, which tracks
     * what is to be rendered.  These are updated when a transaction is
     * submitted rather than at the end as they need to take effect
     * immediately and all transitions need to result in IPC events which
     * should not be skipped.
     */
    struct hayward_window *focused_window;
    struct hayward_workspace *focused_workspace;

    struct {
        struct wl_signal new_node;
    } events;
};

struct hayward_root *
root_create(void);

void
root_destroy(struct hayward_root *root);

struct hayward_workspace *
root_workspace_for_pid(pid_t pid);

void
root_record_workspace_pid(pid_t pid);

void
root_remove_workspace_pid(pid_t pid);

struct hayward_output *
root_find_output(
    bool (*test)(struct hayward_output *output, void *data), void *data
);

void
root_get_box(struct hayward_root *root, struct wlr_box *box);

void
root_rename_pid_workspaces(const char *old_name, const char *new_name);

void
root_add_workspace(struct hayward_workspace *workspace);
void
root_remove_workspace(struct hayward_workspace *workspace);

void
root_sort_workspaces(void);

void
root_set_active_workspace(struct hayward_workspace *workspace);
struct hayward_workspace *
root_get_active_workspace(void);
struct hayward_workspace *
root_get_current_active_workspace(void);

void
root_set_active_output(struct hayward_output *output);
struct hayward_output *
root_get_active_output(void);
struct hayward_output *
root_get_current_active_output(void);

/**
 * Helper functions that traverse the tree to focus the right window.
 */
void
root_set_focused_window(struct hayward_window *window);

/**
 * The active window is the window that is currently selected.  If the active
 * window is meant to be receiving input events then it will also be set as the
 * focused window.  The focused window will be NULL if a layer or other surface
 * is receiving input events.
 */
struct hayward_window *
root_get_active_window(void);
struct hayward_window *
root_get_focused_window(void);

void
root_set_focused_layer(struct wlr_layer_surface_v1 *layer);

/**
 * Directly set the WLRoots surface that should receive input events.
 *
 * This is mostly used by XWayland to focus unmanaged surfaces.
 */
void
root_set_focused_surface(struct wlr_surface *surface);

struct wlr_layer_surface_v1 *
root_get_focused_layer(void);

struct wlr_surface *
root_get_focused_surface(void);

void
root_commit_focus(void);

void
root_for_each_workspace(
    void (*f)(struct hayward_workspace *workspace, void *data), void *data
);

void
root_for_each_window(
    void (*f)(struct hayward_window *window, void *data), void *data
);

struct hayward_workspace *
root_find_workspace(
    bool (*test)(struct hayward_workspace *workspace, void *data), void *data
);

struct hayward_window *
root_find_window(
    bool (*test)(struct hayward_window *window, void *data), void *data
);

#endif
