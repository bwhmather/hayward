#ifndef HWD_TREE_ROOT_H
#define HWD_TREE_ROOT_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include <hayward-common/list.h>

#include <hayward/config.h>
#include <hayward/control/hwd_workspace_management_v1.h>
#include <hayward/tree/window.h>

#include <config.h>

struct hwd_pid_workspaces;

struct hwd_root_state {
    list_t *workspaces;

    /**
     * An optional explicitly focused surface.   Will only be used if there
     * is no active window or layer set.
     */
    struct wlr_surface *focused_surface;

    struct hwd_workspace *active_workspace;
    struct hwd_output *active_output;

    /**
     * An optional layer (top/bottom/side bar) that should receive input
     * events.  If set, will take priority over any active window or
     * explicitly focused surface.
     */
    struct wlr_layer_surface_v1 *focused_layer;
};

struct hwd_root {
    struct hwd_root_state pending;
    struct hwd_root_state committed;
    struct hwd_root_state current;

    bool dirty;

    struct wlr_output_layout *output_layout;

#if HAVE_XWAYLAND
    struct wl_list xwayland_unmanaged; // hwd_xwayland_unmanaged::link
#endif
    struct wl_list drag_icons; // hwd_drag_icon::link

    // Includes disabled outputs
    struct wl_list all_outputs; // hwd_output::link

    list_t *outputs; // struct hwd_output

    // For when there's no connected outputs
    struct hwd_output *fallback_output;

    struct hwd_workspace_manager_v1 * workspace_manager;
    struct wl_list pid_workspaces;

    struct wlr_scene *root_scene;
    struct wlr_scene_tree *orphans;
    struct {
        struct wlr_scene_tree *workspaces;
        struct wlr_scene_tree *outputs;
        struct wlr_scene_tree *unmanaged;
        struct wlr_scene_tree *popups;
    } layers;

    struct wl_listener output_layout_change;
    struct wl_listener transaction_before_commit;
    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
};

struct hwd_root *
root_create(struct wl_display *display);

void
root_destroy(struct hwd_root *root);

void
root_add_workspace(struct hwd_root *root, struct hwd_workspace *workspace);
void
root_remove_workspace(struct hwd_root *root, struct hwd_workspace *workspace);

void
root_set_active_workspace(struct hwd_root *root, struct hwd_workspace *workspace);
struct hwd_workspace *
root_get_active_workspace(struct hwd_root *root);

void
root_set_active_output(struct hwd_root *root, struct hwd_output *output);
struct hwd_output *
root_get_active_output(struct hwd_root *root);

/**
 * Helper functions that traverse the tree to focus the right window.
 */
void
root_set_focused_window(struct hwd_root *root, struct hwd_window *window);

/**
 * The active window is the window that is currently selected.  If the active
 * window is meant to be receiving input events then it will also be set as the
 * focused window.  The focused window will be NULL if a layer or other surface
 * is receiving input events.
 */
struct hwd_window *
root_get_focused_window(struct hwd_root *root);

void
root_set_focused_layer(struct hwd_root *root, struct wlr_layer_surface_v1 *layer);

/**
 * Directly set the WLRoots surface that should receive input events.
 *
 * This is mostly used by XWayland to focus unmanaged surfaces.
 */
void
root_set_focused_surface(struct hwd_root *root, struct wlr_surface *surface);

struct wlr_layer_surface_v1 *
root_get_focused_layer(struct hwd_root *root);

struct wlr_surface *
root_get_focused_surface(struct hwd_root *root);

void
root_for_each_workspace(
    struct hwd_root *root, void (*f)(struct hwd_workspace *workspace, void *data), void *data
);

struct hwd_workspace *
root_find_workspace(
    struct hwd_root *root, bool (*test)(struct hwd_workspace *workspace, void *data), void *data
);

struct hwd_output *
root_find_closest_output(struct hwd_root *root, double x, double y);

#endif
