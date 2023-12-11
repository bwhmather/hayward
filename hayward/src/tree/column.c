#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/tree/column.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/log.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

static bool
column_is_alive(struct hwd_column *column);

static void
column_detach(struct hwd_column *column);

static void
column_init_scene(struct hwd_column *column) {
    column->scene_tree = wlr_scene_tree_create(root->orphans);
    hwd_assert(column->scene_tree != NULL, "Allocation failed");

    column->layers.child_tree = wlr_scene_tree_create(column->scene_tree);
    hwd_assert(column->layers.child_tree != NULL, "Allocation failed");

    const float preview_color[] = {0.125, 0.267, 0.374, 0.8};
    column->layers.preview_box =
        wlr_scene_rect_create(column->scene_tree, 0, 0, (const float *)preview_color);
    hwd_assert(column->layers.preview_box != NULL, "Allocation failed");
}

static void
column_update_scene(struct hwd_column *column) {
    struct wl_list *link = &column->layers.child_tree->children;

    if (column->committed.children->length) {
        // Anchor top most child at top of stack.
        list_t *children = column->committed.children;
        int child_index = children->length - 1;

        struct hwd_window *child = children->items[child_index];
        wlr_scene_node_reparent(&child->scene_tree->node, column->layers.child_tree);
        wlr_scene_node_raise_to_top(&child->scene_tree->node);

        struct hwd_window *prev_child = child;

        // Move subsequent children immediately below it.
        while (child_index > 0) {
            child_index--;

            child = children->items[child_index];
            if (child->committed.fullscreen) {
                continue;
            }

            wlr_scene_node_reparent(&child->scene_tree->node, column->layers.child_tree);
            wlr_scene_node_place_below(&child->scene_tree->node, &prev_child->scene_tree->node);

            prev_child = child;
        }

        link = &prev_child->scene_tree->node.link;
    }

    // Iterate over any nodes that haven't been moved to the top as a result
    // of belonging to a child and unparent them.
    link = link->prev;
    while (link != &column->layers.child_tree->children) {
        struct wlr_scene_node *node = wl_container_of(link, node, link);
        link = link->prev;
        if (node->parent == column->layers.child_tree) {
            wlr_scene_node_reparent(node, root->orphans); // TODO
        }
    }

    if (column->committed.show_preview) {
        wlr_scene_node_set_position(
            &column->layers.preview_box->node, column->committed.preview_box.x + 3,
            column->committed.preview_box.y + 3
        );
        wlr_scene_rect_set_size(
            column->layers.preview_box, column->committed.preview_box.width - 6,
            column->committed.preview_box.height - 6
        );
        wlr_scene_node_set_enabled(&column->layers.preview_box->node, true);
    } else {
        wlr_scene_node_set_enabled(&column->layers.preview_box->node, false);
    }
}

static void
column_destroy_scene(struct hwd_column *column) {
    hwd_assert(
        wl_list_empty(&column->layers.child_tree->children),
        "Can't destroy scene tree of column with children"
    );

    wlr_scene_node_destroy(&column->scene_tree->node);
}

static void
column_destroy(struct hwd_column *column);

static void
column_copy_state(struct hwd_column_state *tgt, struct hwd_column_state *src) {
    list_t *tgt_children = tgt->children;

    memcpy(tgt, src, sizeof(struct hwd_column_state));

    tgt->children = tgt_children;
    list_clear(tgt->children);
    list_cat(tgt->children, src->children);
}

static void
column_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hwd_column *column = wl_container_of(listener, column, transaction_commit);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    wl_list_remove(&listener->link);
    column->dirty = false;

    wl_signal_add(&transaction_manager->events.apply, &column->transaction_apply);

    column_copy_state(&column->committed, &column->pending);
}

static void
column_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_column *column = wl_container_of(listener, column, transaction_apply);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    wl_list_remove(&listener->link);

    column_update_scene(column);

    if (column->committed.dead) {
        wl_signal_add(&transaction_manager->events.after_apply, &column->transaction_after_apply);
    }

    column_copy_state(&column->current, &column->committed);
}

static void
column_handle_transaction_after_apply(struct wl_listener *listener, void *data) {
    struct hwd_column *column = wl_container_of(listener, column, transaction_after_apply);

    wl_list_remove(&listener->link);

    hwd_assert(column->current.dead, "After apply called on live column");
    column_destroy(column);
}

struct hwd_column *
column_create(void) {
    struct hwd_column *column = calloc(1, sizeof(struct hwd_column));
    if (!column) {
        hwd_log(HWD_ERROR, "Unable to allocate hwd_column");
        return NULL;
    }

    static size_t next_id = 1;
    column->id = next_id++;

    column_init_scene(column);

    wl_signal_init(&column->events.begin_destroy);
    wl_signal_init(&column->events.destroy);

    column->pending.layout = L_STACKED;
    column->alpha = 1.0f;

    column->pending.children = create_list();
    column->committed.children = create_list();
    column->current.children = create_list();

    column->transaction_commit.notify = column_handle_transaction_commit;
    column->transaction_apply.notify = column_handle_transaction_apply;
    column->transaction_after_apply.notify = column_handle_transaction_after_apply;

    return column;
}

static bool
column_is_alive(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    return !column->pending.dead;
}

static void
column_destroy(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->current.dead, "Tried to free column which wasn't marked as destroying");

    column_destroy_scene(column);

    list_free(column->pending.children);
    list_free(column->committed.children);
    list_free(column->current.children);

    free(column);
}

static void
column_begin_destroy(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column_is_alive(column), "Expected live column");

    column->pending.dead = true;

    if (column->pending.workspace) {
        column_detach(column);
    }

    wl_signal_emit_mutable(&column->events.begin_destroy, column);

    column_set_dirty(column);
}

void
column_consider_destroy(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    struct hwd_workspace *workspace = column->pending.workspace;

    if (column->pending.children->length) {
        return;
    }
    column_begin_destroy(column);

    if (workspace) {
        workspace_consider_destroy(workspace);
    }
}

void
column_set_dirty(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    if (column->dirty) {
        return;
    }

    column->dirty = true;
    wl_signal_add(&transaction_manager->events.commit, &column->transaction_commit);
    hwd_transaction_manager_ensure_queued(transaction_manager);

    for (int i = 0; i < column->committed.children->length; i++) {
        struct hwd_window *child = column->committed.children->items[i];
        if (window_is_alive(child)) {
            window_set_dirty(child);
        }
    }

    for (int i = 0; i < column->pending.children->length; i++) {
        struct hwd_window *child = column->pending.children->items[i];
        window_set_dirty(child);
    }
}

static void
column_detach(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    struct hwd_workspace *workspace = column->pending.workspace;

    if (workspace == NULL) {
        return;
    }

    workspace_remove_column(workspace, column);
}

void
column_reconcile(
    struct hwd_column *column, struct hwd_workspace *workspace, struct hwd_output *output
) {
    hwd_assert(column != NULL, "Expected column");

    column->pending.workspace = workspace;
    column->pending.output = output;

    if (workspace_is_visible(workspace) && workspace->pending.focus_mode == F_TILING &&
        column == workspace->pending.active_column) {
        column->pending.focused = true;
    } else {
        column->pending.focused = false;
    }

    for (int child_index = 0; child_index < column->pending.children->length; child_index++) {
        struct hwd_window *child = column->pending.children->items[child_index];
        window_reconcile_tiling(child, column);
    }
}

void
column_reconcile_detached(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");

    column->pending.workspace = NULL;
    column->pending.output = NULL;

    column->pending.focused = false;

    for (int child_index = 0; child_index < column->pending.children->length; child_index++) {
        struct hwd_window *child = column->pending.children->items[child_index];
        window_reconcile_tiling(child, column);
    }
}

static void
column_arrange_split(struct hwd_column *column) {
    struct hwd_window *child = NULL;
    list_t *children = column->pending.children;

    if (!children->length) {
        column->pending.preview_target = NULL;
        column->pending.preview_box.x = column->pending.x;
        column->pending.preview_box.y = column->pending.y;
        column->pending.preview_box.width = column->pending.width;
        column->pending.preview_box.height = column->pending.height;
        column_set_dirty(column);
        return;
    }

    struct wlr_box box;
    column_get_box(column, &box);

    double preview_titlebar_height = 30; // TODO TODO TODO

    double visible_height_fraction = 0.0;
    double available_content_height = box.height;
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        visible_height_fraction += child->height_fraction;
        available_content_height -= child->pending.titlebar_height;
    }
    if (column->pending.show_preview) {
        visible_height_fraction += column->preview_height_fraction;
        available_content_height -= preview_titlebar_height;
    }

    // Distance between top of next window and top of the screen.
    double y_offset = 0;

    // The distance, in layout coordinates, between the desired location of the
    // vertical anchor point in the preview and the top of the preview.
    double preview_baseline = round(column->preview_baseline * column->preview_height_fraction);

    // Absolute distance between preview baseline and anchor point if preview is
    // inserted before this one.
    double baseline_delta;

    // Absolute distance between preview baseline and anchor point if preview is
    // inserted after this one.
    double next_baseline_delta;

    bool preview_inserted = false;

    next_baseline_delta = fabs(column->pending.y + preview_baseline - column->preview_anchor_y);

    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        double window_height = child->pending.titlebar_height;
        window_height +=
            available_content_height * child->height_fraction / visible_height_fraction;
        child->pending.shaded = false;

        baseline_delta = next_baseline_delta;
        next_baseline_delta = fabs(
            column->pending.y + round(y_offset + window_height) + preview_baseline -
            column->preview_anchor_y
        );
        if (column->pending.show_preview && !preview_inserted &&
            next_baseline_delta > baseline_delta) {

            double preview_height = preview_titlebar_height;
            preview_height += available_content_height * column->preview_height_fraction /
                visible_height_fraction;

            column->pending.preview_target = window_get_previous_sibling(child);
            column->pending.preview_box.x = column->pending.x;
            column->pending.preview_box.y = column->pending.y + round(y_offset);
            column->pending.preview_box.width = column->pending.width;
            column->pending.preview_box.height = round(preview_height);

            preview_inserted = true;

            y_offset += preview_height;
        }

        child->pending.x = column->pending.x;
        child->pending.y = column->pending.y + round(y_offset);
        child->pending.width = box.width;
        child->pending.height = round(window_height);

        y_offset += child->pending.height;
    }

    if (column->pending.show_preview && !preview_inserted) {
        double preview_height = preview_titlebar_height;
        preview_height +=
            available_content_height * column->preview_height_fraction / visible_height_fraction;

        column->pending.preview_target = child;
        column->pending.preview_box.x = column->pending.x;
        column->pending.preview_box.y = column->pending.y + round(y_offset);
        column->pending.preview_box.width = column->pending.width;
        column->pending.preview_box.height = round(preview_height);

        preview_inserted = true;

        y_offset += preview_height;
    }
}

static void
column_arrange_stacked(struct hwd_column *column) {
    struct hwd_window *child = NULL;
    list_t *children = column->pending.children;
    struct hwd_window *active_child = column->pending.active_child;
    if (column->pending.show_preview) {
        active_child = NULL;
    }

    if (!children->length) {
        column->pending.preview_target = NULL;
        column->pending.preview_box.x = column->pending.x;
        column->pending.preview_box.y = column->pending.y;
        column->pending.preview_box.width = column->pending.width;
        column->pending.preview_box.height = column->pending.height;
        column_set_dirty(column);
        return;
    }

    struct wlr_box box;
    column_get_box(column, &box);

    double preview_titlebar_height = 30; // TODO TODO TODO

    double available_content_height = box.height;
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        available_content_height -= child->pending.titlebar_height;
    }
    if (column->pending.show_preview) {
        available_content_height -= preview_titlebar_height;
    }

    // Distance between top of next window and top of the screen.
    double y_offset = 0;

    // The distance, in layout coordinates, between the desired location of the
    // vertical anchor point in the preview and the top of the preview.
    double preview_baseline = round(column->preview_baseline * column->preview_height_fraction);

    // Absolute distance between preview baseline and anchor point if preview is
    // inserted before this one.
    double baseline_delta;

    // Absolute distance between preview baseline and anchor point if preview is
    // inserted after this one.
    double next_baseline_delta;

    bool preview_inserted = false;

    next_baseline_delta = fabs(column->pending.y + preview_baseline - column->preview_anchor_y);

    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        double window_height = child->pending.titlebar_height;
        if (child != active_child) {
            child->pending.shaded = true;
        } else {
            window_height += available_content_height;
            child->pending.shaded = false;
        }

        baseline_delta = next_baseline_delta;
        next_baseline_delta = fabs(
            column->pending.y + round(y_offset + window_height) + preview_baseline -
            column->preview_anchor_y
        );
        if (column->pending.show_preview && !preview_inserted &&
            next_baseline_delta > baseline_delta) {

            double preview_height = preview_titlebar_height + available_content_height;

            column->pending.preview_target = window_get_previous_sibling(child);
            column->pending.preview_box.x = column->pending.x;
            column->pending.preview_box.y = column->pending.y + round(y_offset);
            column->pending.preview_box.width = column->pending.width;
            column->pending.preview_box.height = round(preview_height);

            preview_inserted = true;

            y_offset += preview_height;
        }

        child->pending.x = column->pending.x;
        child->pending.y = column->pending.y + round(y_offset);
        child->pending.width = box.width;
        child->pending.height = round(window_height);

        y_offset += child->pending.height;

        // TODO Make last visible child use remaining height of parent
    }

    if (column->pending.show_preview && !preview_inserted) {
        double preview_height = preview_titlebar_height + available_content_height;

        column->pending.preview_target = child;
        column->pending.preview_box.x = column->pending.x;
        column->pending.preview_box.y = column->pending.y + round(y_offset);
        column->pending.preview_box.width = column->pending.width;
        column->pending.preview_box.height = round(preview_height);

        preview_inserted = true;

        y_offset += preview_height;
    }
}

void
column_arrange(struct hwd_column *column) {
    if (config->reloading) {
        return;
    }

    switch (column->pending.layout) {
    case L_SPLIT:
        column_arrange_split(column);
        break;
    case L_STACKED:
        column_arrange_stacked(column);
        break;
    default:
        hwd_assert(false, "Unsupported layout");
        break;
    }

    list_t *children = column->pending.children;
    for (int i = 0; i < children->length; ++i) {
        struct hwd_window *child = children->items[i];
        window_arrange(child);
    }
    column_set_dirty(column);
}

struct hwd_window *
column_find_child(
    struct hwd_column *column, bool (*test)(struct hwd_window *window, void *data), void *data
) {
    hwd_assert(column != NULL, "Expected column");
    if (!column->pending.children) {
        return NULL;
    }
    for (int i = 0; i < column->pending.children->length; ++i) {
        struct hwd_window *child = column->pending.children->items[i];
        if (test(child, data)) {
            return child;
        }
    }
    return NULL;
}

struct hwd_window *
column_get_first_child(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");

    list_t *children = column->pending.children;

    if (children->length == 0) {
        return NULL;
    }

    return children->items[0];
}

struct hwd_window *
column_get_last_child(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");

    list_t *children = column->pending.children;

    if (children->length == 0) {
        return NULL;
    }

    return children->items[children->length - 1];
}

void
column_insert_child(struct hwd_column *column, struct hwd_window *window, int i) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(window != NULL, "Expected child");
    hwd_assert(i >= 0 && i <= column->pending.children->length, "Expected index to be in bounds");

    hwd_assert(
        !window->pending.workspace && !window->pending.parent,
        "Windows must be detatched before they can be added to a column"
    );
    if (column->pending.children->length == 0) {
        column->pending.active_child = window;
    }
    list_insert(column->pending.children, i, window);

    window_reconcile_tiling(window, column);

    window_handle_fullscreen_reparent(window);
}

void
column_add_sibling(struct hwd_window *fixed, struct hwd_window *active, bool after) {
    hwd_assert(fixed != NULL, "Expected fixed window");
    hwd_assert(active != NULL, "Expected window");
    hwd_assert(
        !active->pending.workspace && !active->pending.parent,
        "Windows must be detatched before they can be added to a column"
    );

    struct hwd_column *column = fixed->pending.parent;
    hwd_assert(column != NULL, "Expected fixed window to be tiled");

    list_t *siblings = column->pending.children;

    int index = list_find(siblings, fixed);
    hwd_assert(index != -1, "Could not find sibling in child array");

    list_insert(siblings, index + after, active);

    window_reconcile_tiling(fixed, column);
    window_reconcile_tiling(active, column);

    window_handle_fullscreen_reparent(active);
}

void
column_add_child(struct hwd_column *column, struct hwd_window *window) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(
        !window->pending.workspace && !window->pending.workspace,
        "Windows must be detatched before they can be added to a column"
    );
    if (column->pending.children->length == 0) {
        column->pending.active_child = window;
    }
    list_add(column->pending.children, window);

    window_reconcile_tiling(window, column);

    window_handle_fullscreen_reparent(window);
    window_set_dirty(window);
    column_set_dirty(column);
}

void
column_remove_child(struct hwd_column *column, struct hwd_window *window) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(window->pending.parent == column, "Window is not a child of column");

    int index = list_find(column->pending.children, window);
    hwd_assert(index != -1, "Window missing from column child list");

    list_del(column->pending.children, index);

    if (column->pending.active_child == window) {
        if (column->pending.children->length) {
            column->pending.active_child =
                column->pending.children->items[index > 0 ? index - 1 : 0];
            window_reconcile_tiling(column->pending.active_child, column);
        } else {
            column->pending.active_child = NULL;
        }
    }

    window_reconcile_detached(window);
}

void
column_set_active_child(struct hwd_column *column, struct hwd_window *window) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(window->pending.parent == column, "Window is not a child of column");

    struct hwd_window *prev_active = column->pending.active_child;

    if (window == prev_active) {
        return;
    }

    column->pending.active_child = window;

    window_reconcile_tiling(window, column);
    window_set_dirty(window);

    if (prev_active) {
        window_reconcile_tiling(prev_active, column);
        window_set_dirty(prev_active);
    }

    column_set_dirty(column);
}

void
column_get_box(struct hwd_column *column, struct wlr_box *box) {
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(box != NULL, "Expected box");

    box->x = column->pending.x;
    box->y = column->pending.y;
    box->width = column->pending.width;
    box->height = column->pending.height;
}

/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void
column_set_resizing(struct hwd_column *column, bool resizing) {
    hwd_assert(column != NULL, "Expected column");

    if (!column) {
        return;
    }

    for (int i = 0; i < column->pending.children->length; ++i) {
        struct hwd_window *child = column->pending.children->items[i];
        window_set_resizing(child, resizing);
    }
}

static bool
find_urgent_iterator(struct hwd_window *window, void *data) {
    return window->view && view_is_urgent(window->view);
}

bool
column_has_urgent_child(struct hwd_column *column) {
    hwd_assert(column != NULL, "Expected column");
    return column_find_child(column, find_urgent_iterator, NULL);
}
