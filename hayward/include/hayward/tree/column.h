#ifndef HWD_TREE_COLUMN_H
#define HWD_TREE_COLUMN_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>

enum hwd_column_layout {
    L_SPLIT,
    L_STACKED,
};

struct hwd_column_state {
    // Position and size in layout coordinates.
    double x, y;
    double width, height;

    enum hwd_column_layout layout;

    // Cached backlink to containing workspace.
    struct hwd_workspace *workspace;

    // Backling to output.  This is actually the golden source, but should
    // always be updated using the reconciliation functions.
    struct hwd_output *output;

    // Cached flag indicating whether the column contains the focused
    // window.  Should only be updated using the reconciliation functions.
    bool focused;

    list_t *children; // struct hwd_window

    struct hwd_window *active_child;

    bool dead;
};

struct hwd_column {
    size_t id;

    struct hwd_column_state pending;
    struct hwd_column_state committed;
    struct hwd_column_state current;

    bool dirty;

    // For C_ROOT, this has no meaning
    // For other types, this is the position in layout coordinates
    // Includes borders
    double saved_x, saved_y;
    double saved_width, saved_height;

    // The share of the space of parent workspace this container occupies.
    double width_fraction;

    // The share of space of the parent workspace that all children occupy.
    // Used for doing the resize calculations
    double child_total_width;

    float alpha;

    struct wlr_scene_tree *scene_tree;

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
column_consider_destroy(struct hwd_column *container);

void
column_set_dirty(struct hwd_column *column);

void
column_reconcile(
    struct hwd_column *column, struct hwd_workspace *workspace, struct hwd_output *output
);
void
column_reconcile_detached(struct hwd_column *column);

/**
 * Search a container's descendants a container based on test criteria. Returns
 * the first container that passes the test.
 */
struct hwd_window *
column_find_child(
    struct hwd_column *container, bool (*test)(struct hwd_window *view, void *data), void *data
);

void
column_insert_child(struct hwd_column *parent, struct hwd_window *child, int i);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void
column_add_sibling(struct hwd_window *parent, struct hwd_window *child, bool after);

void
column_add_child(struct hwd_column *parent, struct hwd_window *child);

void
column_remove_child(struct hwd_column *parent, struct hwd_window *child);

void
column_set_active_child(struct hwd_column *column, struct hwd_window *window);

/**
 * Get a column's box in layout coordinates.
 */
void
column_get_box(struct hwd_column *column, struct wlr_box *box);

void
column_set_resizing(struct hwd_column *column, bool resizing);

list_t *
column_get_siblings(struct hwd_column *column);

int
column_sibling_index(struct hwd_column *child);

struct hwd_column *
column_get_previous_sibling(struct hwd_column *column);
struct hwd_column *
column_get_next_sibling(struct hwd_column *column);

bool
column_has_urgent_child(struct hwd_column *column);

#endif
