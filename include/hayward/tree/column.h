#ifndef HWD_TREE_COLUMN_H
#define HWD_TREE_COLUMN_H

#include <stdbool.h>
#include <stddef.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward/list.h>

enum hwd_column_layout {
    L_SPLIT,
    L_STACKED,
};

struct hwd_column_state {
    // Position and size in layout coordinates.
    double x, y;
    double width, height;

    // Cached flags indicating whether the column is the first or last column on
    // the column's output.
    bool is_first_child;
    bool is_last_child;

    list_t *children; // struct hwd_window

    // Whether the column should render a preview of the effect of inserting a
    // new window.  `preview_target` is an optional pointer to a child window
    // that the new window will be inserted after.
    bool show_preview;
    struct hwd_window *preview_target; // Populated by `arrange_column`.
    struct wlr_box preview_box;        // Populated by `arrange_column`.

    bool dead;
};

struct hwd_column {
    size_t id;

    struct hwd_column_state pending;
    struct hwd_column_state committed;
    struct hwd_column_state current;

    bool dirty;
    bool dead;

    list_t *children; // struct hwd_window
    struct hwd_window *active_child;

    enum hwd_column_layout layout;

    // Cached backlink to containing workspace.
    struct hwd_workspace *workspace;

    // Backling to output.  This is actually the golden source, but should
    // always be updated using the reconciliation functions.
    struct hwd_output *output;

    // "Fraction" of vertical space allocated to the preview, if visible.  Not
    // included when normalizing.
    double preview_height_fraction;

    // Fraction of distance from top of preview that should be lined up with the
    // anchor.
    double preview_baseline;
    // Absolute cursor location at time preview was created.
    double preview_anchor_x;
    double preview_anchor_y;

    // The share of the space of parent workspace this column occupies.
    double width_fraction;

    // The share of space of the parent workspace that all children occupy.
    // Used for doing the resize calculations
    double child_total_width;

    struct wlr_scene_tree *scene_tree;

    struct {
        struct wlr_scene_tree *child_tree;
        struct wlr_scene_rect *preview_box;
    } layers;

    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal begin_destroy;
        struct wl_signal destroy;
    } events;
};

struct hwd_column *
column_create(void);

void
column_consider_destroy(struct hwd_column *column);

void
column_set_dirty(struct hwd_column *column);

void
column_arrange(struct hwd_column *column);

struct hwd_window *
column_find_child(
    struct hwd_column *column, bool (*test)(struct hwd_window *view, void *data), void *data
);

struct hwd_window *
column_get_first_child(struct hwd_column *column);

struct hwd_window *
column_get_last_child(struct hwd_column *column);

void
column_insert_child(struct hwd_column *column, struct hwd_window *child, int i);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void
column_add_sibling(struct hwd_window *fixed, struct hwd_window *active, bool after);

void
column_add_child(struct hwd_column *column, struct hwd_window *child);

void
column_remove_child(struct hwd_column *column, struct hwd_window *child);

void
column_set_active_child(struct hwd_column *column, struct hwd_window *window);

/**
 * Get a column's box in layout coordinates.
 */
void
column_get_box(struct hwd_column *column, struct wlr_box *box);

void
column_set_resizing(struct hwd_column *column, bool resizing);

#endif
