#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/column.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/globals/root.h>
#include <hayward/output.h>
#include <hayward/transaction.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

static void
column_init_scene(struct hayward_column *column) {
    column->scene_tree = wlr_scene_tree_create(root->orphans);
    hayward_assert(column->scene_tree != NULL, "Allocation failed");
}

static void
column_update_scene(struct hayward_column *column) {
    double x = column->committed.x;
    double y = column->committed.y;

    wlr_scene_node_set_position(&column->scene_tree->node, x, y);

    struct wl_list *link = &column->scene_tree->children;

    if (column->committed.children->length) {
        // Anchor top most child at top of stack.
        list_t *children = column->committed.children;
        int child_index = children->length - 1;

        struct hayward_window *child = children->items[child_index];
        wlr_scene_node_reparent(&child->scene_tree->node, column->scene_tree);
        wlr_scene_node_raise_to_top(&child->scene_tree->node);

        struct hayward_window *prev_child = child;

        // Move subsequent children immediately below it.
        while (child_index > 0) {
            child_index--;

            child = children->items[child_index];
            if (child->committed.fullscreen) {
                continue;
            }

            wlr_scene_node_reparent(
                &child->scene_tree->node, column->scene_tree
            );
            wlr_scene_node_place_below(
                &child->scene_tree->node, &prev_child->scene_tree->node
            );

            prev_child = child;
        }

        link = &prev_child->scene_tree->node.link;
    }

    // Iterate over any nodes that haven't been moved to the top as a result
    // of belonging to a child and unparent them.
    link = link->prev;
    while (link != &column->scene_tree->children) {
        struct wlr_scene_node *node = wl_container_of(link, node, link);
        link = link->prev;
        if (node->parent == column->scene_tree) {
            wlr_scene_node_reparent(node, root->orphans); // TODO
        }
    }
}

static void
column_destroy_scene(struct hayward_column *column) {
    hayward_assert(
        wl_list_empty(&column->scene_tree->children),
        "Can't destroy scene tree of column with children"
    );

    wlr_scene_node_destroy(&column->scene_tree->node);
}

static void
column_destroy(struct hayward_column *column);

static void
column_copy_state(
    struct hayward_column_state *tgt, struct hayward_column_state *src
) {
    list_t *tgt_children = tgt->children;

    memcpy(tgt, src, sizeof(struct hayward_column_state));

    tgt->children = tgt_children;
    list_clear(tgt->children);
    list_cat(tgt->children, src->children);
}

static void
column_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hayward_column *column =
        wl_container_of(listener, column, transaction_commit);

    wl_list_remove(&listener->link);
    column->dirty = false;

    transaction_add_apply_listener(&column->transaction_apply);

    column_copy_state(&column->committed, &column->pending);
}

static void
column_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hayward_column *column =
        wl_container_of(listener, column, transaction_apply);

    wl_list_remove(&listener->link);

    column_update_scene(column);

    if (column->committed.dead) {
        transaction_add_after_apply_listener(&column->transaction_after_apply);
    }

    column_copy_state(&column->current, &column->committed);
}

static void
column_handle_transaction_after_apply(
    struct wl_listener *listener, void *data
) {
    struct hayward_column *column =
        wl_container_of(listener, column, transaction_after_apply);

    wl_list_remove(&listener->link);

    hayward_assert(column->current.dead, "After apply called on live column");
    column_destroy(column);
}

struct hayward_column *
column_create(void) {
    struct hayward_column *column = calloc(1, sizeof(struct hayward_column));
    if (!column) {
        hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_column");
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
    column->transaction_after_apply.notify =
        column_handle_transaction_after_apply;

    return column;
}

bool
column_is_alive(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    return !column->pending.dead;
}

static void
column_destroy(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(
        column->current.dead,
        "Tried to free column which wasn't marked as destroying"
    );

    column_destroy_scene(column);

    list_free(column->pending.children);
    list_free(column->committed.children);
    list_free(column->current.children);

    free(column);
}

static void
column_begin_destroy(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(column_is_alive(column), "Expected live column");

    column->pending.dead = true;

    if (column->pending.workspace) {
        column_detach(column);
    }

    wl_signal_emit(&column->events.begin_destroy, column);

    column_set_dirty(column);
}

void
column_consider_destroy(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    struct hayward_workspace *workspace = column->pending.workspace;

    if (column->pending.children->length) {
        return;
    }
    column_begin_destroy(column);

    if (workspace) {
        workspace_consider_destroy(workspace);
    }
}

void
column_set_dirty(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");

    if (column->dirty) {
        return;
    }

    column->dirty = true;
    transaction_add_commit_listener(&column->transaction_commit);
    transaction_ensure_queued();

    for (int i = 0; i < column->committed.children->length; i++) {
        struct hayward_window *child = column->committed.children->items[i];
        if (window_is_alive(child)) {
            window_set_dirty(child);
        }
    }

    for (int i = 0; i < column->pending.children->length; i++) {
        struct hayward_window *child = column->pending.children->items[i];
        window_set_dirty(child);
    }
}

void
column_detach(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    struct hayward_workspace *workspace = column->pending.workspace;

    if (workspace == NULL) {
        return;
    }

    workspace_remove_tiling(workspace, column);
}

void
column_reconcile(
    struct hayward_column *column, struct hayward_workspace *workspace,
    struct hayward_output *output
) {
    hayward_assert(column != NULL, "Expected column");

    column->pending.workspace = workspace;
    column->pending.output = output;

    if (workspace_is_visible(workspace) &&
        workspace->pending.focus_mode == F_TILING &&
        column == workspace->pending.active_column) {
        column->pending.focused = true;
    } else {
        column->pending.focused = false;
    }

    for (int child_index = 0; child_index < column->pending.children->length;
         child_index++) {
        struct hayward_window *child =
            column->pending.children->items[child_index];
        window_reconcile_tiling(child, column);
    }
}

void
column_reconcile_detached(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");

    column->pending.workspace = NULL;
    column->pending.output = NULL;

    column->pending.focused = false;

    for (int child_index = 0; child_index < column->pending.children->length;
         child_index++) {
        struct hayward_window *child =
            column->pending.children->items[child_index];
        window_reconcile_tiling(child, column);
    }
}

struct hayward_window *
column_find_child(
    struct hayward_column *column,
    bool (*test)(struct hayward_window *window, void *data), void *data
) {
    hayward_assert(column != NULL, "Expected column");
    if (!column->pending.children) {
        return NULL;
    }
    for (int i = 0; i < column->pending.children->length; ++i) {
        struct hayward_window *child = column->pending.children->items[i];
        if (test(child, data)) {
            return child;
        }
    }
    return NULL;
}

void
column_insert_child(
    struct hayward_column *column, struct hayward_window *child, int i
) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(child != NULL, "Expected child");
    hayward_assert(
        i >= 0 && i <= column->pending.children->length,
        "Expected index to be in bounds"
    );

    hayward_assert(
        !child->pending.workspace && !child->pending.parent,
        "Windows must be detatched before they can be added to a column"
    );
    if (column->pending.children->length == 0) {
        column->pending.active_child = child;
    }
    list_insert(column->pending.children, i, child);

    window_reconcile_tiling(child, column);

    window_handle_fullscreen_reparent(child);
}

void
column_add_sibling(
    struct hayward_window *fixed, struct hayward_window *active, bool after
) {
    hayward_assert(fixed != NULL, "Expected fixed window");
    hayward_assert(active != NULL, "Expected window");
    hayward_assert(
        !active->pending.workspace && !active->pending.parent,
        "Windows must be detatched before they can be added to a column"
    );

    struct hayward_column *column = fixed->pending.parent;
    hayward_assert(column != NULL, "Expected fixed window to be tiled");

    list_t *siblings = column->pending.children;

    int index = list_find(siblings, fixed);
    hayward_assert(index != -1, "Could not find sibling in child array");

    list_insert(siblings, index + after, active);

    window_reconcile_tiling(fixed, column);
    window_reconcile_tiling(active, column);

    window_handle_fullscreen_reparent(active);
}

void
column_add_child(struct hayward_column *column, struct hayward_window *child) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(child != NULL, "Expected window");
    hayward_assert(
        !child->pending.workspace && !child->pending.workspace,
        "Windows must be detatched before they can be added to a column"
    );
    if (column->pending.children->length == 0) {
        column->pending.active_child = child;
    }
    list_add(column->pending.children, child);

    window_reconcile_tiling(child, column);

    window_handle_fullscreen_reparent(child);
    window_set_dirty(child);
    column_set_dirty(column);
}

void
column_remove_child(
    struct hayward_column *column, struct hayward_window *child
) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(child != NULL, "Expected window");
    hayward_assert(
        child->pending.parent == column, "Window is not a child of column"
    );

    int index = list_find(column->pending.children, child);
    hayward_assert(index != -1, "Window missing from column child list");

    list_del(column->pending.children, index);

    if (column->pending.active_child == child) {
        if (column->pending.children->length) {
            column->pending.active_child =
                column->pending.children->items[index > 0 ? index - 1 : 0];
            window_reconcile_tiling(column->pending.active_child, column);
        } else {
            column->pending.active_child = NULL;
        }
    }

    window_reconcile_detached(child);
}

void
column_set_active_child(
    struct hayward_column *column, struct hayward_window *window
) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(
        window->pending.parent == column, "Window is not a child of column"
    );

    struct hayward_window *prev_active = column->pending.active_child;

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
column_get_box(struct hayward_column *column, struct wlr_box *box) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(box != NULL, "Expected box");

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
column_set_resizing(struct hayward_column *column, bool resizing) {
    hayward_assert(column != NULL, "Expected column");

    if (!column) {
        return;
    }

    for (int i = 0; i < column->pending.children->length; ++i) {
        struct hayward_window *child = column->pending.children->items[i];
        window_set_resizing(child, resizing);
    }
}

list_t *
column_get_siblings(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");

    if (column->pending.workspace) {
        return column->pending.workspace->pending.tiling;
    }
    return NULL;
}

int
column_sibling_index(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");

    return list_find(column_get_siblings(column), column);
}

struct hayward_column *
column_get_previous_sibling(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(
        column->pending.workspace, "Column is not attached to a workspace"
    );

    list_t *siblings = column->pending.workspace->pending.tiling;
    int index = list_find(siblings, column);

    if (index <= 0) {
        return NULL;
    }

    return siblings->items[index - 1];
}

struct hayward_column *
column_get_next_sibling(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(
        column->pending.workspace, "Column is not attached to a workspace"
    );

    list_t *siblings = column->pending.workspace->pending.tiling;
    int index = list_find(siblings, column);

    if (index < 0) {
        return NULL;
    }

    if (index >= siblings->length - 1) {
        return NULL;
    }

    return siblings->items[index + 1];
}

static bool
find_urgent_iterator(struct hayward_window *window, void *data) {
    return window->view && view_is_urgent(window->view);
}

bool
column_has_urgent_child(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    return column_find_child(column, find_urgent_iterator, NULL);
}
