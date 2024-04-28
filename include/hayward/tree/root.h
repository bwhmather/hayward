#ifndef HWD_TREE_ROOT_H
#define HWD_TREE_ROOT_H

#include <config.h>

#include <stdbool.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>

#include <hayward/config.h>
#include <hayward/desktop/hwd_workspace_management_v1.h>
#include <hayward/list.h>
#include <hayward/theme.h>

struct hwd_window;

struct hwd_root_state {
    struct hwd_workspace *workspace;

    /**
     * An optional layer (top/bottom/side bar) that should receive input
     * events.  If set, will take priority over any active window or
     * explicitly focused surface.
     */
    struct wlr_layer_surface_v1 *active_layer;

    /**
     * An optional unmangaged surface which should receive input events.  Takes
     * priority over active layer and active window.
     */
    struct wlr_surface *active_unmanaged;

    struct hwd_theme *theme;
};

struct hwd_root {
    struct hwd_root_state pending;
    struct hwd_root_state committed;
    struct hwd_root_state current;

    bool dirty;

    struct hwd_transaction_manager *transaction_manager;

    list_t *workspaces;
    struct hwd_workspace *active_workspace;

    struct wlr_surface *focused_surface;
    struct hwd_window *focused_window;
    struct wlr_layer_surface_v1 *focused_layer;

    struct wlr_output_layout *output_layout;
    struct wlr_scene_output_layout *scene_output_layout;

#if HAVE_XWAYLAND
    struct wl_list xwayland_unmanaged; // hwd_xwayland_unmanaged::link
#endif
    struct wl_list drag_icons; // hwd_drag_icon::link

    // Includes disabled outputs
    struct wl_list all_outputs; // hwd_output::link

    list_t *outputs; // struct hwd_output
    struct hwd_output *active_output;

    struct hwd_workspace_manager_v1 *workspace_manager;

    // Previously applied theme that should be cleaned up after transaction
    // apply.
    struct hwd_theme *orphaned_theme;

    struct wlr_scene *root_scene;
    struct {
        struct wlr_scene_tree *background;
        struct wlr_scene_tree *workspaces;
        struct wlr_scene_tree *unmanaged;
        struct wlr_scene_tree *moving;
        struct wlr_scene_tree *overlay;
        struct wlr_scene_tree *popups;
    } layers;

    struct wl_listener output_layout_change;

    struct wl_listener transaction_before_commit;
    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal focus_changed; // struct hwd_root_focus_changed_event
        struct wl_signal scene_changed; // struct hwd_root
    } events;
};

struct hwd_root_focus_changed_event {
    struct hwd_root *root;
    struct wlr_surface *old_focus;
    struct wlr_surface *new_focus;
};

struct hwd_root *
root_create(struct wl_display *display);

void
root_destroy(struct hwd_root *root);

void
root_set_dirty(struct hwd_root *root);

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

struct wlr_surface *
root_get_focused_unmanaged(struct hwd_root *root);

struct wlr_layer_surface_v1 *
root_get_focused_layer(struct hwd_root *root);

/**
 * The active window is the window that is currently selected.  If the active
 * window is meant to be receiving input events then it will also be set as the
 * focused window.  The focused window will be NULL if a layer or other surface
 * is receiving input events.
 */
struct hwd_window *
root_get_focused_window(struct hwd_root *root);

struct wlr_surface *
root_get_focused_surface(struct hwd_root *root);

void
root_set_focused_layer(struct hwd_root *root, struct wlr_layer_surface_v1 *layer);

/**
 * Helper functions that traverse the tree to focus the right window.
 */
void
root_set_focused_window(struct hwd_root *root, struct hwd_window *window);

/**
 * Directly set the WLRoots surface that should receive input events.
 *
 * This is mostly used by XWayland to focus unmanaged surfaces.
 */
void
root_set_focused_surface(struct hwd_root *root, struct wlr_surface *surface);

void
root_commit_focus(struct hwd_root *root);

struct hwd_workspace *
root_find_workspace(
    struct hwd_root *root, bool (*test)(struct hwd_workspace *workspace, void *data), void *data
);

struct hwd_output *
root_find_closest_output(struct hwd_root *root, double x, double y);

struct hwd_output *
root_get_output_at(struct hwd_root *root, double x, double y);

struct hwd_output *
root_get_output_in_direction(
    struct hwd_root *root, struct hwd_output *reference, enum wlr_direction direction
);

/**
 * Passes a new theme to replace the current one.  The `root` takes ownership of
 * the theme and is responsible for destroying it once done.
 */
void
root_set_theme(struct hwd_root *root, struct hwd_theme *theme);

struct hwd_theme *
root_get_theme(struct hwd_root *root);

struct hwd_transaction_manager *
root_get_transaction_manager(struct hwd_root *root);

#endif
