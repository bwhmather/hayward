#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/column.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

static void
column_init_scene(struct hwd_column *column) {
    column->scene_tree = wlr_scene_tree_create(NULL);
    assert(column->scene_tree != NULL);

    column->layers.child_tree = wlr_scene_tree_create(column->scene_tree);
    assert(column->layers.child_tree != NULL);

    const float preview_color[] = {0.125, 0.267, 0.374, 0.8};
    column->layers.preview_box =
        wlr_scene_rect_create(column->scene_tree, 0, 0, (const float *)preview_color);
    assert(column->layers.preview_box != NULL);
}

static void
column_update_scene(struct hwd_column *column) {
    struct wl_list *link = &column->layers.child_tree->children;

    list_t *children = column->committed.children;
    struct hwd_window *prev_child = NULL;
    for (int child_index = children->length - 1; child_index >= 0; child_index--) {
        struct hwd_window *child = children->items[child_index];
        if (child->committed.fullscreen) {
            continue;
        }

        wlr_scene_node_reparent(&child->scene_tree->node, column->layers.child_tree);
        if (prev_child != NULL) {
            wlr_scene_node_place_below(&child->scene_tree->node, &prev_child->scene_tree->node);
        } else {
            wlr_scene_node_raise_to_top(&child->scene_tree->node);
        }

        prev_child = child;
    }
    if (prev_child != NULL) {
        link = &prev_child->scene_tree->node.link;
    }

    // Iterate over any nodes that haven't been moved to the top as a result
    // of belonging to a child and unparent them.
    link = link->prev;
    while (link != &column->layers.child_tree->children) {
        struct wlr_scene_node *node = wl_container_of(link, node, link);
        link = link->prev;
        if (node->parent == column->layers.child_tree) {
            wlr_scene_node_reparent(node, NULL);
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
    assert(wl_list_empty(&column->layers.child_tree->children));

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

    assert(column->current.dead);
    column_destroy(column);
}

struct hwd_column *
column_create(void) {
    struct hwd_column *column = calloc(1, sizeof(struct hwd_column));
    if (!column) {
        wlr_log(WLR_ERROR, "Unable to allocate hwd_column");
        return NULL;
    }

    static size_t next_id = 1;
    column->id = next_id++;

    column_init_scene(column);

    wl_signal_init(&column->events.begin_destroy);
    wl_signal_init(&column->events.destroy);

    column->layout = L_STACKED;

    column->pending.children = create_list();
    column->committed.children = create_list();
    column->current.children = create_list();

    column->children = create_list();

    column->transaction_commit.notify = column_handle_transaction_commit;
    column->transaction_apply.notify = column_handle_transaction_apply;
    column->transaction_after_apply.notify = column_handle_transaction_after_apply;

    return column;
}

static void
column_destroy(struct hwd_column *column) {
    assert(column != NULL);
    assert(column->current.dead);

    column_destroy_scene(column);

    list_free(column->children);

    list_free(column->pending.children);
    list_free(column->committed.children);
    list_free(column->current.children);

    free(column);
}

void
column_consider_destroy(struct hwd_column *column) {
    assert(column != NULL);

    if (column->dead) {
        return;
    }

    if (column->children->length) {
        return;
    }

    struct hwd_workspace *workspace = column->workspace;
    assert(workspace != NULL);

    struct hwd_output *output = column->output;
    assert(output != NULL);

    int index = list_find(workspace->columns, column);
    assert(index != -1);

    list_del(workspace->columns, index);

    if (workspace->active_column == column) {
        struct hwd_column *next_active = NULL;

        for (int candidate_index = 0; candidate_index < workspace->columns->length;
             candidate_index++) {
            struct hwd_column *candidate = workspace->columns->items[candidate_index];

            if (candidate->output != output) {
                continue;
            }

            if (next_active != NULL && candidate_index >= index) {
                break;
            }

            next_active = candidate;
        }

        workspace->active_column = next_active;

        if (next_active != NULL) {
            column_set_dirty(next_active);
        }
    }

    wl_signal_emit_mutable(&column->events.begin_destroy, column);

    if (workspace) {
        workspace_consider_destroy(workspace);
    }

    column_set_dirty(column);
    workspace_set_dirty(workspace);
}

void
column_set_dirty(struct hwd_column *column) {
    assert(column != NULL);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    if (column->dirty) {
        return;
    }

    column->dirty = true;
    wl_signal_add(&transaction_manager->events.commit, &column->transaction_commit);
    hwd_transaction_manager_ensure_queued(transaction_manager);
}

static void
column_arrange_split(struct hwd_column *column) {
    struct hwd_window *child = NULL;

    list_t *children = column->pending.children;
    list_clear(children);
    for (int i = 0; i < column->children->length; ++i) {
        struct hwd_window *window = column->children->items[i];
        list_add(children, window);
    }

    struct wlr_box box;
    column_get_box(column, &box);

    double preview_titlebar_height = 30; // TODO TODO TODO

    double visible_height_fraction = 0.0;
    double available_content_height = box.height;
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        if (window_is_fullscreen(child)) {
            continue;
        }
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
        if (window_is_fullscreen(child)) {
            continue;
        }

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

    for (int i = 0; i < column->pending.children->length; i++) {
        struct hwd_window *window = column->pending.children->items[i];
        window_set_dirty(window);
    }
}

static void
column_arrange_stacked(struct hwd_column *column) {
    struct hwd_window *child = NULL;

    list_t *children = column->pending.children;
    list_clear(children);
    for (int i = 0; i < column->children->length; ++i) {
        struct hwd_window *window = column->children->items[i];
        list_add(children, window);
    }

    struct hwd_window *active_child = column->active_child;
    if (column->pending.show_preview) {
        active_child = NULL;
    }

    struct wlr_box box;
    column_get_box(column, &box);

    double preview_titlebar_height = 30; // TODO TODO TODO

    double available_content_height = box.height;
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        if (window_is_fullscreen(child)) {
            continue;
        }
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
        if (window_is_fullscreen(child)) {
            continue;
        }

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

    for (int i = 0; i < column->pending.children->length; i++) {
        struct hwd_window *window = column->pending.children->items[i];
        window_set_dirty(window);
    }
}

void
column_arrange(struct hwd_column *column) {
    HWD_PROFILER_TRACE();

    if (column->dirty) {
        column->pending.dead = column->dead;

        switch (column->layout) {
        case L_SPLIT:
            column_arrange_split(column);
            break;
        case L_STACKED:
            column_arrange_stacked(column);
            break;
        default:
            assert(false);
            break;
        }
    }

    for (int i = 0; i < column->pending.children->length; i++) {
        struct hwd_window *window = column->pending.children->items[i];
        window_arrange(window);
    }
}

struct hwd_window *
column_find_child(
    struct hwd_column *column, bool (*test)(struct hwd_window *window, void *data), void *data
) {
    assert(column != NULL);
    if (!column->children) {
        return NULL;
    }
    for (int i = 0; i < column->children->length; ++i) {
        struct hwd_window *child = column->children->items[i];
        if (test(child, data)) {
            return child;
        }
    }
    return NULL;
}

struct hwd_window *
column_get_first_child(struct hwd_column *column) {
    assert(column != NULL);

    list_t *children = column->children;

    if (children->length == 0) {
        return NULL;
    }

    return children->items[0];
}

struct hwd_window *
column_get_last_child(struct hwd_column *column) {
    assert(column != NULL);

    list_t *children = column->children;

    if (children->length == 0) {
        return NULL;
    }

    return children->items[children->length - 1];
}

void
column_insert_child(struct hwd_column *column, struct hwd_window *window, int i) {
    assert(column != NULL);
    assert(window != NULL);
    assert(i >= 0 && i <= column->children->length);

    assert(!window->workspace && !window->column);
    if (column->children->length == 0) {
        column->active_child = window;
    }
    list_insert(column->children, i, window);

    window_reconcile_tiling(window, column);

    column_set_dirty(column);
}

void
column_add_sibling(struct hwd_window *fixed, struct hwd_window *active, bool after) {
    assert(fixed != NULL);
    assert(active != NULL);
    assert(!active->workspace && !active->column);

    struct hwd_column *column = fixed->column;
    assert(column != NULL);

    list_t *siblings = column->children;

    int index = list_find(siblings, fixed);
    assert(index != -1);

    list_insert(siblings, index + after, active);

    window_reconcile_tiling(fixed, column);
    window_reconcile_tiling(active, column);

    column_set_dirty(column);
}

void
column_add_child(struct hwd_column *column, struct hwd_window *window) {
    assert(column != NULL);
    assert(window != NULL);
    assert(!window->workspace && !window->column);
    if (column->children->length == 0) {
        column->active_child = window;
    }
    list_add(column->children, window);

    window_reconcile_tiling(window, column);

    column_set_dirty(column);
}

void
column_remove_child(struct hwd_column *column, struct hwd_window *window) {
    assert(column != NULL);
    assert(window != NULL);
    assert(window->column == column);

    int index = list_find(column->children, window);
    assert(index != -1);

    list_del(column->children, index);

    if (column->active_child == window) {
        if (column->children->length) {
            column->active_child = column->children->items[index > 0 ? index - 1 : 0];
            window_reconcile_tiling(column->active_child, column);
        } else {
            column->active_child = NULL;
        }
    }

    window_reconcile_detached(window);

    column_set_dirty(column);
}

void
column_set_active_child(struct hwd_column *column, struct hwd_window *window) {
    assert(column != NULL);
    assert(window != NULL);
    assert(window->column == column);

    struct hwd_window *prev_active = column->active_child;

    if (window == prev_active) {
        return;
    }

    column->active_child = window;

    window_reconcile_tiling(window, column);

    if (prev_active) {
        window_reconcile_tiling(prev_active, column);
    }

    column_set_dirty(column);
}

void
column_get_box(struct hwd_column *column, struct wlr_box *box) {
    assert(column != NULL);
    assert(box != NULL);

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
    assert(column != NULL);

    if (!column) {
        return;
    }

    for (int i = 0; i < column->children->length; ++i) {
        struct hwd_window *child = column->children->items[i];
        window_set_resizing(child, resizing);
    }
}
