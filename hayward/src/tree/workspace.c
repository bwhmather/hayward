#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/workspace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/globals/root.h>
#include <hayward/globals/transaction.h>
#include <hayward/ipc-server.h>
#include <hayward/output.h>
#include <hayward/transaction.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#include <config.h>

static void
workspace_destroy(struct hwd_workspace *workspace);

static struct hwd_window *
workspace_find_window(
    struct hwd_workspace *workspace, bool (*test)(struct hwd_window *window, void *data), void *data
);

static void
workspace_init_scene(struct hwd_workspace *workspace) {
    workspace->scene_tree = wlr_scene_tree_create(root->orphans);
    hwd_assert(workspace->scene_tree != NULL, "Allocation failed");

    workspace->layers.tiling = wlr_scene_tree_create(workspace->scene_tree);
    hwd_assert(workspace->layers.tiling != NULL, "Allocation failed");

    workspace->layers.floating = wlr_scene_tree_create(workspace->scene_tree);
    hwd_assert(workspace->layers.floating != NULL, "Allocation failed");

    workspace->layers.fullscreen = wlr_scene_tree_create(workspace->scene_tree);
    hwd_assert(workspace->layers.fullscreen != NULL, "Allocation failed");
}

static void
workspace_update_layer_tiling(struct hwd_workspace *workspace) {
    struct wl_list *link = &workspace->layers.tiling->children;

    if (workspace->committed.tiling->length) {
        // Anchor top most column at top of stack.
        list_t *columns = workspace->committed.tiling;
        int column_index = columns->length - 1;

        struct hwd_column *column = columns->items[column_index];
        wlr_scene_node_reparent(&column->scene_tree->node, workspace->layers.tiling);
        wlr_scene_node_raise_to_top(&column->scene_tree->node);

        struct hwd_column *prev_column = column;

        // Move subsequent columns immediately below it.
        while (column_index > 0) {
            column_index--;

            column = columns->items[column_index];
            wlr_scene_node_reparent(&column->scene_tree->node, workspace->layers.tiling);
            wlr_scene_node_place_below(&column->scene_tree->node, &prev_column->scene_tree->node);

            prev_column = column;
        }

        link = &prev_column->scene_tree->node.link;
    }

    // Iterate over any nodes that haven't been moved to the top as a result
    // of belonging to a child and unparent them.
    link = link->prev;
    while (link != &workspace->layers.tiling->children) {
        struct wlr_scene_node *node = wl_container_of(link, node, link);
        link = link->prev;
        if (node->parent == workspace->layers.tiling) {
            wlr_scene_node_reparent(node, root->orphans); // TODO
        }
    }
}

static void
workspace_update_layer_floating(struct hwd_workspace *workspace) {
    struct wl_list *link = &workspace->layers.floating->children;

    if (workspace->committed.floating->length) {
        // Anchor top most window at top of stack.
        list_t *windows = workspace->committed.floating;
        int window_index = windows->length - 1;

        struct hwd_window *window = windows->items[window_index];
        wlr_scene_node_reparent(&window->scene_tree->node, workspace->layers.floating);
        wlr_scene_node_raise_to_top(&window->scene_tree->node);

        struct hwd_window *prev_window = window;

        // Move subsequent windows immediately below it.
        while (window_index > 0) {
            window_index--;

            window = windows->items[window_index];
            if (window->committed.fullscreen) {
                continue;
            }

            wlr_scene_node_reparent(&window->scene_tree->node, workspace->layers.floating);
            wlr_scene_node_place_above(&window->scene_tree->node, &prev_window->scene_tree->node);

            prev_window = window;
        }

        link = &prev_window->scene_tree->node.link;
    }

    // Iterate over any nodes that haven't been moved to the top as a result
    // of belonging to a child and unparent them.
    link = link->prev;
    while (link != &workspace->layers.floating->children) {
        struct wlr_scene_node *node = wl_container_of(link, node, link);
        link = link->prev;
        if (node->parent == workspace->layers.floating) {
            wlr_scene_node_reparent(node, root->orphans); // TODO
        }
    }
}

static void
workspace_update_scene(struct hwd_workspace *workspace) {
    wlr_scene_node_set_enabled(&workspace->scene_tree->node, workspace->committed.focused);

    workspace_update_layer_tiling(workspace);
    workspace_update_layer_floating(workspace);
}

static void
workspace_destroy_scene(struct hwd_workspace *workspace) {
    wlr_scene_node_destroy(&workspace->scene_tree->node);
}

static void
workspace_copy_state(struct hwd_workspace_state *tgt, struct hwd_workspace_state *src) {
    list_t *tgt_floating = tgt->floating;
    list_t *tgt_tiling = tgt->tiling;

    memcpy(tgt, src, sizeof(struct hwd_workspace_state));

    tgt->floating = tgt_floating;
    list_clear(tgt->floating);
    list_cat(tgt->floating, src->floating);

    tgt->tiling = tgt_tiling;
    list_clear(tgt->tiling);
    list_cat(tgt->tiling, src->tiling);
}

static void
workspace_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hwd_workspace *workspace = wl_container_of(listener, workspace, transaction_commit);

    wl_list_remove(&listener->link);
    workspace->dirty = false;

    wl_signal_add(&transaction_manager->events.apply, &workspace->transaction_apply);

    workspace_copy_state(&workspace->committed, &workspace->pending);
}

static void
workspace_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_workspace *workspace = wl_container_of(listener, workspace, transaction_apply);

    wl_list_remove(&listener->link);

    workspace_update_scene(workspace);

    if (workspace->committed.dead) {
        wlr_scene_node_set_enabled(&workspace->scene_tree->node, false);
        wl_signal_add(
            &transaction_manager->events.after_apply, &workspace->transaction_after_apply
        );
    }

    workspace_copy_state(&workspace->current, &workspace->committed);
}

static void
workspace_handle_transaction_after_apply(struct wl_listener *listener, void *data) {
    struct hwd_workspace *workspace = wl_container_of(listener, workspace, transaction_after_apply);

    wl_list_remove(&listener->link);

    hwd_assert(workspace->current.dead, "After apply called on live workspace");
    workspace_destroy(workspace);
}

struct hwd_workspace *
workspace_create(const char *name) {
    struct hwd_workspace *workspace = calloc(1, sizeof(struct hwd_workspace));
    if (!workspace) {
        hwd_log(HWD_ERROR, "Unable to allocate hwd_workspace");
        return NULL;
    }

    static size_t next_id = 1;
    workspace->id = next_id++;

    wl_signal_init(&workspace->events.begin_destroy);

    workspace->transaction_commit.notify = workspace_handle_transaction_commit;
    workspace->transaction_apply.notify = workspace_handle_transaction_apply;
    workspace->transaction_after_apply.notify = workspace_handle_transaction_after_apply;

    workspace->name = name ? strdup(name) : NULL;

    workspace->pending.floating = create_list();
    workspace->pending.tiling = create_list();
    workspace->committed.floating = create_list();
    workspace->committed.tiling = create_list();
    workspace->current.floating = create_list();
    workspace->current.tiling = create_list();

    workspace_init_scene(workspace);

    ipc_event_workspace(NULL, workspace, "init");

    return workspace;
}

bool
workspace_is_alive(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");
    return !workspace->pending.dead;
}

static void
workspace_destroy(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(
        workspace->current.dead, "Tried to free workspace which wasn't marked as destroying"
    );
    hwd_assert(
        !workspace->dirty, "Tried to free workspace which is queued for the next transaction"
    );

    workspace_destroy_scene(workspace);

    free(workspace->name);
    list_free(workspace->pending.floating);
    list_free(workspace->pending.tiling);
    list_free(workspace->committed.floating);
    list_free(workspace->committed.tiling);
    list_free(workspace->current.floating);
    list_free(workspace->current.tiling);
    free(workspace);
}

void
workspace_begin_destroy(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(workspace_is_alive(workspace), "Expected live workspace");

    hwd_log(HWD_DEBUG, "Destroying workspace '%s'", workspace->name);

    workspace->pending.dead = true;

    ipc_event_workspace(NULL, workspace, "empty"); // intentional

    workspace_detach(workspace);

    wl_signal_emit_mutable(&workspace->events.begin_destroy, workspace);

    workspace_set_dirty(workspace);
}

void
workspace_consider_destroy(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.tiling->length) {
        return;
    }

    if (workspace->pending.floating->length) {
        return;
    }

    if (workspace->pending.focused) {
        return;
    }

    workspace_begin_destroy(workspace);
}

void
workspace_set_dirty(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(
        hwd_transaction_manager_transaction_in_progress(transaction_manager),
        "Expected active transaction"
    );

    if (workspace->dirty) {
        return;
    }
    workspace->dirty = true;
    wl_signal_add(&transaction_manager->events.commit, &workspace->transaction_commit);
    hwd_transaction_manager_ensure_queued(transaction_manager);

    for (int i = 0; i < workspace->committed.floating->length; i++) {
        struct hwd_window *window = workspace->committed.floating->items[i];
        window_set_dirty(window);
    }
    for (int i = 0; i < workspace->committed.tiling->length; i++) {
        struct hwd_column *column = workspace->committed.tiling->items[i];
        column_set_dirty(column);
    }

    for (int i = 0; i < workspace->pending.floating->length; i++) {
        struct hwd_window *window = workspace->pending.floating->items[i];
        window_set_dirty(window);
    }

    for (int i = 0; i < workspace->pending.tiling->length; i++) {
        struct hwd_column *column = workspace->pending.tiling->items[i];
        column_set_dirty(column);
    }
}

static bool
_workspace_by_name(struct hwd_workspace *workspace, void *data) {
    return strcasecmp(workspace->name, data) == 0;
}

struct hwd_workspace *
workspace_by_name(const char *name) {
    return root_find_workspace(root, _workspace_by_name, (void *)name);
}

bool
workspace_is_visible(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.dead) {
        return false;
    }

    return workspace->pending.focused;
}

static bool
find_urgent_iterator(struct hwd_window *window, void *data) {
    return window->view && view_is_urgent(window->view);
}

void
workspace_detect_urgent(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    bool new_urgent = (bool)workspace_find_window(workspace, find_urgent_iterator, NULL);

    if (workspace->urgent != new_urgent) {
        workspace->urgent = new_urgent;
        ipc_event_workspace(NULL, workspace, "urgent");
    }
}

void
workspace_detach(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.root != NULL) {
        root_remove_workspace(workspace->pending.root, workspace);
    }
}

void
workspace_reconcile(struct hwd_workspace *workspace, struct hwd_root *root) {
    hwd_assert(workspace != NULL, "Expected workspace");

    bool dirty = false;

    if (workspace->pending.root != root) {
        workspace->pending.root = root;
        dirty = true;
    }

    bool should_focus = workspace == root_get_active_workspace(root);
    if (should_focus != workspace->pending.focused) {
        workspace->pending.focused = should_focus;
        dirty = true;
    }

    if (dirty) {
        for (int column_index = 0; column_index < workspace->pending.tiling->length;
             column_index++) {
            struct hwd_column *column = workspace->pending.tiling->items[column_index];
            column_reconcile(column, workspace, column->pending.output);
        }

        for (int window_index = 0; window_index < workspace->pending.floating->length;
             window_index++) {
            struct hwd_window *window = workspace->pending.floating->items[window_index];
            window_reconcile_floating(window, workspace);
        }
    }
}

void
workspace_reconcile_detached(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    bool dirty = false;

    if (workspace->pending.root != NULL) {
        workspace->pending.root = NULL;
        dirty = true;
    }

    bool should_focus = false;
    if (should_focus != workspace->pending.focused) {
        workspace->pending.focused = should_focus;
        dirty = true;
    }

    if (dirty) {
        for (int column_index = 0; column_index < workspace->pending.tiling->length;
             column_index++) {
            struct hwd_column *column = workspace->pending.tiling->items[column_index];
            column_reconcile(column, workspace, column->pending.output);
        }

        for (int window_index = 0; window_index < workspace->pending.floating->length;
             window_index++) {
            struct hwd_window *window = workspace->pending.floating->items[window_index];
            window_reconcile_floating(window, workspace);
        }
    }
}

void
workspace_add_floating(struct hwd_workspace *workspace, struct hwd_window *window) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(window->pending.parent == NULL, "Window still has a parent");
    hwd_assert(window->pending.workspace == NULL, "Window is already attached to a workspace");

    struct hwd_window *prev_active_floating = workspace_get_active_floating_window(workspace);

    list_add(workspace->pending.floating, window);

    window_reconcile_floating(window, workspace);

    if (prev_active_floating) {
        window_reconcile_floating(prev_active_floating, workspace);
    }

    workspace_set_dirty(workspace);
}

void
workspace_remove_floating(struct hwd_workspace *workspace, struct hwd_window *window) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(window->pending.workspace == workspace, "Window is not a child of workspace");
    hwd_assert(window->pending.parent == NULL, "Window is not floating");

    int index = list_find(workspace->pending.floating, window);
    hwd_assert(index != -1, "Window missing from floating list");

    list_del(workspace->pending.floating, index);

    if (workspace->pending.floating->length == 0) {
        // Switch back to tiling mode.
        workspace->pending.focus_mode = F_TILING;

        struct hwd_window *next_active = workspace_get_active_tiling_window(workspace);
        if (next_active != NULL) {
            window_reconcile_tiling(next_active, next_active->pending.parent);
        }
    } else {
        // Focus next floating window.
        window_reconcile_floating(workspace_get_active_floating_window(workspace), workspace);
    }

    if (window->pending.output && window->pending.fullscreen) {
        output_reconcile(window->pending.output);
    }

    window_reconcile_detached(window);
}

void
workspace_insert_tiling(
    struct hwd_workspace *workspace, struct hwd_output *output, struct hwd_column *column, int index
) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(output != NULL, "Expected output");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == NULL, "Column is already attached to a workspace");
    hwd_assert(column->pending.output == NULL, "Column is already attached to an output");
    hwd_assert(
        index >= 0 && index <= workspace->pending.tiling->length, "Column index not in bounds"
    );

    list_insert(workspace->pending.tiling, index, column);
    if (workspace->pending.active_column == NULL) {
        workspace->pending.active_column = column;
    }

    column_reconcile(column, workspace, output);

    workspace_set_dirty(workspace);
    column_set_dirty(column);
}

void
workspace_remove_tiling(struct hwd_workspace *workspace, struct hwd_column *column) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == workspace, "Column is not a child of workspace");

    struct hwd_output *output = column->pending.output;
    hwd_assert(output != NULL, "Expected output");

    int index = list_find(workspace->pending.tiling, column);
    hwd_assert(index != -1, "Column is missing from workspace column list");

    list_del(workspace->pending.tiling, index);

    if (workspace->pending.active_column == column) {
        struct hwd_column *next_active = NULL;

        for (int candidate_index = 0; candidate_index < workspace->pending.tiling->length;
             candidate_index++) {
            struct hwd_column *candidate = workspace->pending.tiling->items[candidate_index];

            if (candidate->pending.output != output) {
                continue;
            }

            if (next_active != NULL && candidate_index >= index) {
                break;
            }

            next_active = candidate;
        }

        workspace->pending.active_column = next_active;

        if (next_active != NULL) {
            column_reconcile(next_active, workspace, output);

            column_set_dirty(next_active);
        }
    }

    column_reconcile_detached(column);

    workspace_set_dirty(workspace);
    column_set_dirty(column);
}

size_t
workspace_num_tiling_views(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    size_t count = 0;

    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hwd_column *column = workspace->pending.tiling->items[i];
        count += column->pending.children->length;
    }

    return count;
}

struct hwd_output *
workspace_get_active_output(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    struct hwd_column *active_column = workspace->pending.active_column;
    if (active_column != NULL) {
        return active_column->pending.output;
    }

    return NULL;
}

struct hwd_window *
workspace_get_active_tiling_window(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    struct hwd_column *active_column = workspace->pending.active_column;
    if (active_column == NULL) {
        return NULL;
    }

    return active_column->pending.active_child;
}

static struct hwd_window *
workspace_get_committed_active_tiling_window(struct hwd_workspace *workspace) {
    hwd_assert(workspace != NULL, "Expected workspace");

    struct hwd_column *active_column = workspace->committed.active_column;
    if (active_column == NULL) {
        return NULL;
    }

    return active_column->committed.active_child;
}

struct hwd_window *
workspace_get_active_floating_window(struct hwd_workspace *workspace) {
    if (workspace->pending.floating->length == 0) {
        return NULL;
    }

    return workspace->pending.floating->items[0];
}

static struct hwd_window *
workspace_get_committed_active_floating_window(struct hwd_workspace *workspace) {
    if (workspace->committed.floating->length == 0) {
        return NULL;
    }

    return workspace->committed.floating->items[0];
}

struct hwd_window *
workspace_get_active_window(struct hwd_workspace *workspace) {
    switch (workspace->pending.focus_mode) {
    case F_TILING:
        return workspace_get_active_tiling_window(workspace);
    case F_FLOATING:
        return workspace_get_active_floating_window(workspace);
    default:
        hwd_abort("Invalid focus mode");
    }
}

struct hwd_window *
workspace_get_committed_active_window(struct hwd_workspace *workspace) {
    switch (workspace->committed.focus_mode) {
    case F_TILING:
        return workspace_get_committed_active_tiling_window(workspace);
    case F_FLOATING:
        return workspace_get_committed_active_floating_window(workspace);
    default:
        hwd_abort("Invalid focus mode");
    }
}

void
workspace_set_active_window(struct hwd_workspace *workspace, struct hwd_window *window) {
    hwd_assert(workspace != NULL, "Expected workspace");

    struct hwd_window *prev_active = workspace_get_active_window(workspace);
    if (window == prev_active) {
        return;
    }

    if (window == NULL) {
        workspace->pending.active_column = NULL;
        workspace->pending.focus_mode = F_TILING;
    } else if (window_is_floating(window)) {
        hwd_assert(window->pending.workspace == workspace, "Window attached to wrong workspace");

        int index = list_find(workspace->pending.floating, window);
        hwd_assert(index != -1, "Window missing from list of floating windows");

        list_del(workspace->pending.floating, index);
        list_add(workspace->pending.floating, window);

        workspace->pending.focus_mode = F_FLOATING;

        window_reconcile_floating(window, workspace);
    } else {
        hwd_assert(window->pending.workspace == workspace, "Window attached to wrong workspace");

        struct hwd_column *old_column = workspace->pending.active_column;
        struct hwd_column *new_column = window->pending.parent;
        hwd_assert(
            new_column->pending.workspace == workspace, "Column attached to wrong workspace"
        );

        column_set_active_child(new_column, window);

        workspace->pending.active_column = new_column;
        workspace->pending.focus_mode = F_TILING;

        struct hwd_root *root = workspace->pending.root;
        if (root_get_active_workspace(root) == workspace) {
            root_set_active_output(root, new_column->pending.output);
        }

        column_reconcile(new_column, workspace, new_column->pending.output);
        if (old_column != NULL && old_column != new_column) {
            column_reconcile(old_column, workspace, old_column->pending.output);
        }
    }

    if (prev_active != NULL) {
        if (window_is_floating(prev_active)) {
            window_reconcile_floating(prev_active, workspace);
        } else {
            window_reconcile_tiling(prev_active, prev_active->pending.parent);
        }
    }

    arrange_workspace(workspace);
}

static bool
is_fullscreen_window_for_output(struct hwd_window *window, void *data) {
    struct hwd_output *output = data;

    if (window->pending.output != output) {
        return false;
    }

    if (!window->pending.fullscreen) {
        return false;
    }

    return true;
}

struct hwd_window *
workspace_get_fullscreen_window_for_output(
    struct hwd_workspace *workspace, struct hwd_output *output
) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(output != NULL, "Expected output");

    return workspace_find_window(workspace, is_fullscreen_window_for_output, output);
}

static struct hwd_window *
workspace_find_window(
    struct hwd_workspace *workspace, bool (*test)(struct hwd_window *window, void *data), void *data
) {
    hwd_assert(workspace != NULL, "Expected workspace");

    struct hwd_window *result = NULL;
    // Tiling
    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hwd_column *child = workspace->pending.tiling->items[i];
        if ((result = column_find_child(child, test, data))) {
            return result;
        }
    }
    // Floating
    for (int i = 0; i < workspace->pending.floating->length; ++i) {
        struct hwd_window *child = workspace->pending.floating->items[i];
        if (test(child, data)) {
            return child;
        }
    }
    return NULL;
}
