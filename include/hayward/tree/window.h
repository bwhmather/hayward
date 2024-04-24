#ifndef HWD_TREE_WINDOW_H
#define HWD_TREE_WINDOW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

#include <hayward/config.h>
#include <hayward/list.h>
#include <hayward/theme.h>

#define MIN_SANE_W 100
#define MIN_SANE_H 60

struct hwd_seat;
struct hwd_root;
struct hwd_output;
struct hwd_workspace;
struct hwd_view;

struct hwd_window_state {
    // Origin of window in layout coordinates.
    double x, y;
    // Width and height of window, including borders and titlebar.  If window is
    // shaded then height should be set to the titlebar height.
    double width, height;

    // Indicates if only the titlebar of the window should be rendered.  Golden
    // source is layout and active child of parent column.  Updated in arrange.
    bool shaded;
    bool fullscreen;

    // Cached reference to currently applicable window theme.
    struct hwd_theme_window *theme;

    // Cached flag indicating whether the window is focused.  Should only be
    // updated by calling one of the `window_reconcile_` functions.
    bool focused;

    // These are in layout coordinates.
    double titlebar_height;
    double border_left;
    double border_right;
    double border_top;
    double border_bottom;
    double content_x, content_y;
    double content_width, content_height;

    bool dead;
};

struct hwd_window {
    size_t id;

    // Forward link to the view.  The window owns the view only if the view's
    // window pointer also points back to the window.
    struct hwd_view *view;

    struct hwd_window_state pending;
    struct hwd_window_state committed;
    struct hwd_window_state current;

    bool dirty;

    // Cached backlink to workspace containing the floating window or  column
    // containing the child window.  Should only be updated by calling one of
    // the `window_reconcile_` functions.
    struct hwd_workspace *workspace;

    // Cached backlink to the column containing the window.  Null if window is
    // not part of a column.  Should only be updated by calling one of the
    // `window_reconcile_` functions.
    struct hwd_column *parent;

    // A list of disabled outputs that this window has been evacuated from, in
    // priority order from highest (earliest) to lowest (most recent).  If the
    // pending output for a window is disabled, the window will be moved to a
    // new output and the old output will be added to the end of this list.  If
    // an output in this list is subsequently re-enabled then it will be set as
    // the pending output and later entries in this list will be cleared.  If a
    // window is moved manually then its history will be cleared.
    //
    // The goal is to make sure that windows always stay exactly where you put
    // them, regardless of how many times outputs are unplugged or reconfigured.
    list_t *output_history; // struct dtl_output *

    // If the window's workspace is current, the window is not moving, this
    // pointer is set and the output it points to is enabled the the window will
    // be fullscreen on this output.  If any of these conditions do not hold
    // then this pointer will be ignored until that changes.
    list_t *fullscreen_output_history; // struct dtl_output *

    // This is the last manually assigned floating position of the window.  If a
    // floating window is made tiling or fullscreen, this will be preserved so
    // that the window can be restored to the same position later.  If it is
    // evacuated to a different screen, this will be preserved and the new
    // current size and position will be derived from it.
    double saved_x, saved_y;
    double saved_width, saved_height;

    // If true, the window has been plucked from the normal plane of existence
    // and is being moved in sync with the mouse.
    bool moving;

    // Identifier tracking the serial of the configure event sent during at the
    // beginning of the current commit.  Used to discard responses for previous
    // configures.
    uint32_t configure_serial;
    bool is_configuring;

    char *title;           // The view's title (unformatted)
    char *formatted_title; // The title displayed in the title bar

    // The fraction of vertical space available for content that should be
    // allocated to this window when the containing column has an un-pinned
    // window focused and this window is pinned.  When floating, this is
    // relative to the average height fraction prior to being extracted from a
    // column.
    double height_fraction;

    struct wlr_texture *title_focused;
    struct wlr_texture *title_focused_inactive;
    struct wlr_texture *title_focused_tab_title;
    struct wlr_texture *title_unfocused;
    struct wlr_texture *title_urgent;

    struct wlr_scene_tree *scene_tree;
    struct wlr_addon scene_tree_marker;

    struct {
        struct wlr_scene_tree *inner_tree;

        struct wlr_scene_node *titlebar;
        struct wlr_scene_node *titlebar_text;
        struct wlr_scene_node *titlebar_button_close;
        struct wlr_scene_node *border;

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

struct hwd_window *
window_create(struct hwd_view *view);

bool
window_is_alive(struct hwd_window *window);

void
window_begin_destroy(struct hwd_window *window);

void
window_set_dirty(struct hwd_window *window);

void
window_detach(struct hwd_window *window);

/**
 * These functions will update cached back links and internal state to match
 * canonical values on parent.
 */
void
window_reconcile_floating(struct hwd_window *window, struct hwd_workspace *workspace);
void
window_reconcile_tiling(struct hwd_window *window, struct hwd_column *column);
void
window_reconcile_detached(struct hwd_window *window);

void
window_set_moving(struct hwd_window *window, bool moving);

void
window_arrange(struct hwd_window *window);

/**
 * Move window out of soon to be disabled output.
 */
void
window_evacuate(struct hwd_window *window, struct hwd_output *output);

/**
 * If the window is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void
window_end_mouse_operation(struct hwd_window *window);

bool
window_is_floating(struct hwd_window *window);

bool
window_is_fullscreen(struct hwd_window *window);

bool
window_is_tiling(struct hwd_window *window);

void
window_fullscreen(struct hwd_window *window);

void
window_fullscreen_on_output(struct hwd_window *window, struct hwd_output *output);

void
window_unfullscreen(struct hwd_window *window);

void
floating_calculate_constraints(int *min_width, int *max_width, int *min_height, int *max_height);

void
window_floating_resize_and_center(struct hwd_window *window);

void
window_floating_set_default_size(struct hwd_window *window);

void
window_floating_move_to(struct hwd_window *window, struct hwd_output *output, double lx, double ly);

void
window_floating_move_to_center(struct hwd_window *window);

struct hwd_output *
window_get_output(struct hwd_window *window);

struct hwd_output *
window_get_fullscreen_output(struct hwd_window *window);

void
window_get_box(struct hwd_window *window, struct wlr_box *box);

void
window_set_resizing(struct hwd_window *window, bool resizing);

void
window_set_geometry_from_content(struct hwd_window *window);

bool
window_is_transient_for(struct hwd_window *child, struct hwd_window *ancestor);

void
window_raise_floating(struct hwd_window *window);

list_t *
window_get_siblings(struct hwd_window *window);

int
window_sibling_index(struct hwd_window *child);

struct hwd_window *
window_get_previous_sibling(struct hwd_window *window);
struct hwd_window *
window_get_next_sibling(struct hwd_window *window);

/**
 * Returns window for which the given node is the `scene_tree` root, otherwise
 * returns NULL.
 */
struct hwd_window *
window_for_scene_node(struct wlr_scene_node *node);

#endif
