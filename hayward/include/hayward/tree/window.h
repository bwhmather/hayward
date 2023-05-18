#ifndef HAYWARD_TREE_WINDOW_H
#define HAYWARD_TREE_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>

#include <hayward/config.h>
#include <hayward/scene/text.h>

#define MIN_SANE_W 100
#define MIN_SANE_H 60

struct hayward_seat;
struct hayward_root;
struct hayward_output;
struct hayward_workspace;
struct hayward_view;

struct hayward_window_state {
    // Origin of window relative to the parent container.  When window is in
    // tiling mode, `x` will always be zero and `y` will be the offset of the
    // top of the titlebar relative to the top of the window.
    double x, y;
    // Width and height of window, including borders and titlebar.  If window is
    // shaded then height should be set to the titlebar height.
    double width, height;

    // Indicates if only the titlebar of the window should be rendered.  Golden
    // source is layout and active child of parent column.  Updated in arrange.
    bool shaded;
    bool fullscreen;

    // Cached backlink to workspace containing the floating window or
    // column containing the child window.  Should only be updated by
    // calling one of the `window_reconcile_` functions.
    struct hayward_workspace *workspace;

    // Cached backlink to the primary output of the window.  If the window
    // is tiling then this will simply be the output of the column.  If the
    // window is floating then it will be the closest output to the centre
    // of the window.  Should only be updated by calling one of the
    // `window_reconcile_` functions.
    struct hayward_output *output;

    // Cached backlink to the column containing the window.  Null if window
    // is not part of a column.  Should only be updated by calling one of
    // the `window_reconcile_` functions.
    struct hayward_column *parent;

    // Cached flag indicating whether the window is focused.  Should only
    // be updated by calling one of the `window_reconcile_` functions.
    bool focused;

    enum hayward_window_border border;
    int border_thickness;
    bool border_top;
    bool border_bottom;
    bool border_left;
    bool border_right;

    // These are in layout coordinates.
    double content_x, content_y;
    double content_width, content_height;

    bool dead;
};

struct hayward_window {
    size_t id;

    struct hayward_view *view;

    struct hayward_window_state pending;
    struct hayward_window_state committed;
    struct hayward_window_state current;

    bool dirty;

    // Identifier tracking the serial of the configure event sent during at the
    // beginning of the current commit.  Used to discard responses for previous
    // configures.
    uint32_t configure_serial;
    bool is_configuring;

    char *title;           // The view's title (unformatted)
    char *formatted_title; // The title displayed in the title bar

    // For C_ROOT, this has no meaning
    // For other types, this is the position in layout coordinates
    // Includes borders
    double saved_x, saved_y;
    double saved_width, saved_height;

    // Used when the view changes to CSD unexpectedly. This will be a non-B_CSD
    // border which we use to restore when the view returns to SSD.
    enum hayward_window_border saved_border;

    // The share of the space of parent column that this window occupies.
    double height_fraction;

    // The share of space of the parent column that all children occupy.
    // Used for doing the resize calculations.
    double child_total_height;

    // In most cases this is the same as the content x and y, but if the view
    // refuses to resize to the content dimensions then it can be smaller.
    // These are in layout coordinates.
    double surface_x, surface_y;

    float alpha;

    struct wlr_texture *title_focused;
    struct wlr_texture *title_focused_inactive;
    struct wlr_texture *title_focused_tab_title;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;

    struct wlr_scene_tree *scene_tree;
    struct wlr_addon scene_tree_marker;

    struct {
        struct wlr_scene_tree *title_tree;
        struct wlr_scene_rect *title_background;
        struct hayward_text_node *title_text;
        // Line separating title from content.
        struct wlr_scene_rect *title_border;

        struct wlr_scene_tree *border_tree;
        struct wlr_scene_rect *border_top;
        struct wlr_scene_rect *border_bottom;
        struct wlr_scene_rect *border_left;
        struct wlr_scene_rect *border_right;

        struct wlr_scene_tree *content_tree;
    } layers;

    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal begin_destroy;
        struct wl_signal destroy;
    } events;
};

struct hayward_window *
window_create(struct hayward_view *view);

bool
window_is_alive(struct hayward_window *window);

void
window_begin_destroy(struct hayward_window *window);

void
window_set_dirty(struct hayward_window *window);

void
window_detach(struct hayward_window *window);
bool
window_is_attached(struct hayward_window *window);

/**
 * These functions will update cached back links and internal state to match
 * canonical values on parent.
 */
void
window_reconcile_floating(
    struct hayward_window *window, struct hayward_workspace *workspace
);
void
window_reconcile_tiling(
    struct hayward_window *window, struct hayward_column *column
);
void
window_reconcile_detached(struct hayward_window *window);

/**
 * If the window is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void
window_end_mouse_operation(struct hayward_window *window);

bool
window_is_floating(struct hayward_window *window);

bool
window_is_fullscreen(struct hayward_window *window);

bool
window_is_tiling(struct hayward_window *window);

/**
 * Return the height of a regular title bar.
 */
size_t
window_titlebar_height(void);

void
window_set_fullscreen(struct hayward_window *window, bool enabled);

void
window_handle_fullscreen_reparent(struct hayward_window *window);

void
floating_calculate_constraints(
    int *min_width, int *max_width, int *min_height, int *max_height
);

void
window_floating_resize_and_center(struct hayward_window *window);

void
window_floating_set_default_size(struct hayward_window *window);

/**
 * Move a floating window to a new layout-local position.
 */
void
window_floating_move_to(
    struct hayward_window *window, struct hayward_output *output, double lx,
    double ly
);

/**
 * Move a floating window to the center of the workspace.
 */
void
window_floating_move_to_center(struct hayward_window *window);

struct hayward_output *
window_get_output(struct hayward_window *window);

/**
 * Get a window's box in layout coordinates.
 */
void
window_get_box(struct hayward_window *window, struct wlr_box *box);

void
window_set_resizing(struct hayward_window *window, bool resizing);

void
window_set_geometry_from_content(struct hayward_window *window);

bool
window_is_transient_for(
    struct hayward_window *child, struct hayward_window *ancestor
);

void
window_raise_floating(struct hayward_window *window);

list_t *
window_get_siblings(struct hayward_window *window);

int
window_sibling_index(struct hayward_window *child);

struct hayward_window *
window_get_previous_sibling(struct hayward_window *window);
struct hayward_window *
window_get_next_sibling(struct hayward_window *window);

/**
 * Returns window for which the given node is the `scene_tree` root, otherwise
 * returns NULL.
 */
struct hayward_window *
window_for_scene_node(struct wlr_scene_node *node);

#endif
