#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/column.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/desktop.h>
#include <hayward/desktop/transaction.h>
#include <hayward/output.h>
#include <hayward/tree/node.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

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

    // Damage the old location
    desktop_damage_column(column);

    column_copy_state(&column->current, &column->committed);

    // Damage the new location
    desktop_damage_column(column);

    if (column->current.dead) {
        column_destroy(column);
    }
}

struct hayward_column *
column_create(void) {
    struct hayward_column *c = calloc(1, sizeof(struct hayward_column));
    if (!c) {
        hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_column");
        return NULL;
    }
    node_init(&c->node, N_COLUMN, c);
    c->pending.layout = L_STACKED;
    c->alpha = 1.0f;

    c->pending.children = create_list();
    c->committed.children = create_list();
    c->current.children = create_list();

    c->transaction_commit.notify = column_handle_transaction_commit;
    c->transaction_apply.notify = column_handle_transaction_apply;

    wl_signal_init(&c->events.destroy);
    wl_signal_emit(&root->events.new_node, &c->node);

    return c;
}

void
column_destroy(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(
        column->current.dead,
        "Tried to free column which wasn't marked as destroying"
    );
    list_free(column->pending.children);
    list_free(column->committed.children);
    list_free(column->current.children);

    free(column);
}

void
column_begin_destroy(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    wl_signal_emit(&column->node.events.destroy, &column->node);

    column->pending.dead = true;

    if (column->pending.workspace) {
        column_detach(column);
    }

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

    for (int i = 0; i < column->committed.children->length; i++) {
        struct hayward_window *child = column->pending.children->items[i];
        window_set_dirty(child);
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
    bool (*test)(struct hayward_window *container, void *data), void *data
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
    struct hayward_column *parent, struct hayward_window *child, int i
) {
    hayward_assert(parent != NULL, "Expected column");
    hayward_assert(child != NULL, "Expected child");
    hayward_assert(
        i >= 0 && i <= parent->pending.children->length,
        "Expected index to be in bounds"
    );

    hayward_assert(
        !child->pending.workspace && !child->pending.parent,
        "Windows must be detatched before they can be added to a column"
    );
    if (parent->pending.children->length == 0) {
        parent->pending.active_child = child;
    }
    list_insert(parent->pending.children, i, child);

    window_reconcile_tiling(child, parent);

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
column_add_child(struct hayward_column *parent, struct hayward_window *child) {
    hayward_assert(parent != NULL, "Expected column");
    hayward_assert(child != NULL, "Expected window");
    hayward_assert(
        !child->pending.workspace && !child->pending.workspace,
        "Windows must be detatched before they can be added to a column"
    );
    if (parent->pending.children->length == 0) {
        parent->pending.active_child = child;
    }
    list_add(parent->pending.children, child);

    window_reconcile_tiling(child, parent);

    window_handle_fullscreen_reparent(child);
    window_set_dirty(child);
    column_set_dirty(parent);
}

void
column_remove_child(
    struct hayward_column *parent, struct hayward_window *child
) {
    hayward_assert(parent != NULL, "Expected column");
    hayward_assert(child != NULL, "Expected window");
    hayward_assert(
        child->pending.parent == parent, "Window is not a child of column"
    );

    int index = list_find(parent->pending.children, child);
    hayward_assert(index != -1, "Window missing from column child list");

    list_del(parent->pending.children, index);

    if (parent->pending.active_child == child) {
        if (parent->pending.children->length) {
            parent->pending.active_child =
                parent->pending.children->items[index > 0 ? index - 1 : 0];
            window_reconcile_tiling(parent->pending.active_child, parent);
        } else {
            parent->pending.active_child = NULL;
        }
    }

    window_reconcile_detached(child);

    column_set_dirty(parent);
    window_set_dirty(child);
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
column_for_each_child(
    struct hayward_column *column,
    void (*f)(struct hayward_window *window, void *data), void *data
) {
    hayward_assert(column != NULL, "Expected column");
    if (column->pending.children) {
        for (int i = 0; i < column->pending.children->length; ++i) {
            struct hayward_window *child = column->pending.children->items[i];
            f(child, data);
        }
    }
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
column_sibling_index(struct hayward_column *child) {
    hayward_assert(child != NULL, "Expected column");

    return list_find(column_get_siblings(child), child);
}

list_t *
column_get_current_siblings(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");

    if (column->current.workspace) {
        return column->current.workspace->pending.tiling;
    }
    return NULL;
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
find_urgent_iterator(struct hayward_window *container, void *data) {
    return container->view && view_is_urgent(container->view);
}

bool
column_has_urgent_child(struct hayward_column *column) {
    hayward_assert(column != NULL, "Expected column");
    return column_find_child(column, find_urgent_iterator, NULL);
}
