#ifndef _HAYWARD_WINDOW_H
#define _HAYWARD_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>

#include "hayward-common/list.h"

#include "hayward/tree/node.h"

enum hayward_window_border {
    B_NONE,
    B_PIXEL,
    B_NORMAL,
    B_CSD,
};

struct hayward_seat;
struct hayward_root;
struct hayward_output;
struct hayward_workspace;
struct hayward_view;

struct hayward_window_state {
    double x, y;
    double width, height;

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
};

struct hayward_window {
    struct hayward_node node;
    struct hayward_view *view;

    struct hayward_window_state current;
    struct hayward_window_state pending;

    char *title;           // The view's title (unformatted)
    char *formatted_title; // The title displayed in the title bar

    // Whether stickiness has been enabled on this window. Use
    // `window_is_sticky_[or_child]` rather than accessing this field
    // directly; it'll also check that the window is floating.
    bool is_sticky;

    // For C_ROOT, this has no meaning
    // For other types, this is the position in layout coordinates
    // Includes borders
    double saved_x, saved_y;
    double saved_width, saved_height;

    // Used when the view changes to CSD unexpectedly. This will be a non-B_CSD
    // border which we use to restore when the view returns to SSD.
    enum hayward_window_border saved_border;

    // The share of the space of parent window this window occupies
    double width_fraction;
    double height_fraction;

    // The share of space of the parent window that all children occupy
    // Used for doing the resize calculations
    double child_total_width;
    double child_total_height;

    // In most cases this is the same as the content x and y, but if the view
    // refuses to resize to the content dimensions then it can be smaller.
    // These are in layout coordinates.
    double surface_x, surface_y;

    // Outputs currently being intersected
    list_t *outputs; // struct hayward_output

    float alpha;

    struct wlr_texture *title_focused;
    struct wlr_texture *title_focused_inactive;
    struct wlr_texture *title_focused_tab_title;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;

    struct {
        struct wl_signal destroy;
    } events;
};

struct hayward_window *window_create(struct hayward_view *view);

void window_destroy(struct hayward_window *window);

void window_begin_destroy(struct hayward_window *window);

void window_detach(struct hayward_window *window);
bool window_is_attached(struct hayward_window *window);

/**
 * These functions will update cached back links and internal state to match
 * canonical values on parent.
 */
void window_reconcile_floating(
    struct hayward_window *window, struct hayward_workspace *workspace
);
void window_reconcile_tiling(
    struct hayward_window *window, struct hayward_column *column
);
void window_reconcile_detached(struct hayward_window *window);

/**
 * If the window is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void window_end_mouse_operation(struct hayward_window *window);

void window_update_title_textures(struct hayward_window *window);

bool window_is_floating(struct hayward_window *window);

bool window_is_current_floating(struct hayward_window *window);

void window_set_floating(struct hayward_window *window, bool enable);

bool window_is_fullscreen(struct hayward_window *window);

bool window_is_tiling(struct hayward_window *window);

/**
 * Detaches a window from its current column, and by extension workspace, and
 * attaches it to a new column, possibly in a different workspace.  Where in
 * the new column it is inserted is determined by the direction of movement.
 *
 * If the window was previously floating or fullscreen, all of that state will
 * be cleared.
 *
 * The window will not be automatically focused.
 *
 * The old workspace and old columns will not be automatically cleaned up.
 */
void window_move_to_column_from_direction(
    struct hayward_window *window, struct hayward_column *column,
    enum wlr_direction move_dir
);

/**
 * Detaches a window from its current column, and by extension workspace, and
 * attaches it to a new column, possibly in a different workspace.  The window
 * will be move immediately after the column's last focused child.
 *
 * If the window was previously floating or fullscreen, all of that state will
 * be cleared.
 *
 * The window will not be automatically focused.
 *
 * The old workspace and old columns will not be automatically cleaned up.
 */
void window_move_to_column(
    struct hayward_window *window, struct hayward_column *destination
);

void window_move_to_workspace(
    struct hayward_window *window, struct hayward_workspace *workspace
);

void window_move_to_output_from_direction(
    struct hayward_window *window, struct hayward_output *output,
    enum wlr_direction move_dir
);
void window_move_to_output(
    struct hayward_window *window, struct hayward_output *output
);

struct wlr_surface *window_surface_at(
    struct hayward_window *window, double lx, double ly, double *sx, double *sy
);

bool window_contents_contain_point(
    struct hayward_window *window, double lx, double ly
);

bool window_contains_point(struct hayward_window *window, double lx, double ly);

/**
 * Returns the fullscreen window obstructing this window if it exists.
 */
struct hayward_window *
window_obstructing_fullscreen_window(struct hayward_window *window);

/**
 * Return the height of a regular title bar.
 */
size_t window_titlebar_height(void);

void window_set_fullscreen(struct hayward_window *window, bool enabled);

void window_handle_fullscreen_reparent(struct hayward_window *window);

void floating_calculate_constraints(
    int *min_width, int *max_width, int *min_height, int *max_height
);

void window_floating_resize_and_center(struct hayward_window *window);

void window_floating_set_default_size(struct hayward_window *window);

/**
 * Move a floating window to a new layout-local position.
 */
void window_floating_move_to(
    struct hayward_window *window, double lx, double ly
);

/**
 * Move a floating window to the center of the workspace.
 */
void window_floating_move_to_center(struct hayward_window *window);

struct hayward_output *window_get_output(struct hayward_window *window);
struct hayward_output *window_get_current_output(struct hayward_window *window);

/**
 * Get a window's box in layout coordinates.
 */
void window_get_box(struct hayward_window *window, struct wlr_box *box);

void window_set_resizing(struct hayward_window *window, bool resizing);

void window_set_geometry_from_content(struct hayward_window *window);

bool window_is_transient_for(
    struct hayward_window *child, struct hayward_window *ancestor
);

void window_raise_floating(struct hayward_window *window);

bool window_is_sticky(struct hayward_window *window);

list_t *window_get_siblings(struct hayward_window *window);

int window_sibling_index(struct hayward_window *child);

list_t *window_get_current_siblings(struct hayward_window *window);

struct hayward_window *window_get_previous_sibling(struct hayward_window *window
);
struct hayward_window *window_get_next_sibling(struct hayward_window *window);

enum hayward_column_layout window_parent_layout(struct hayward_window *window);
enum hayward_column_layout
window_current_parent_layout(struct hayward_window *window);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 * If the window is not on any output, return NULL.
 */
struct hayward_output *window_get_effective_output(struct hayward_window *window
);

void window_discover_outputs(struct hayward_window *window);

#endif
