#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/workspace.h"

#include <config.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/control/hwd_workspace_management_v1.h>
#include <hayward/globals/root.h>
#include <hayward/ipc_server.h>
#include <hayward/output.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

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

    if (workspace->committed.columns->length) {
        // Anchor top most column at top of stack.
        list_t *columns = workspace->committed.columns;
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
            wlr_scene_node_place_below(&window->scene_tree->node, &prev_window->scene_tree->node);

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
    list_t *tgt_columns = tgt->columns;

    memcpy(tgt, src, sizeof(struct hwd_workspace_state));

    tgt->floating = tgt_floating;
    list_clear(tgt->floating);
    list_cat(tgt->floating, src->floating);

    tgt->columns = tgt_columns;
    list_clear(tgt->columns);
    list_cat(tgt->columns, src->columns);
}

static void
workspace_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hwd_workspace *workspace = wl_container_of(listener, workspace, transaction_commit);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    wl_list_remove(&listener->link);
    workspace->dirty = false;

    wl_signal_add(&transaction_manager->events.apply, &workspace->transaction_apply);

    struct hwd_root *root = workspace->pending.root;
    if (root != workspace->committed.root && workspace->workspace_handle != NULL) {
        hwd_workspace_handle_v1_destroy(workspace->workspace_handle);
        workspace->workspace_handle = NULL;
    }

    if (root != NULL) {
        if (workspace->workspace_handle == NULL) {
            workspace->workspace_handle = hwd_workspace_handle_v1_create(root->workspace_manager);
            hwd_workspace_handle_v1_set_name(workspace->workspace_handle, workspace->name);
        }

        hwd_workspace_handle_v1_set_focused(
            workspace->workspace_handle, workspace->pending.focused
        );
    }

    workspace_copy_state(&workspace->committed, &workspace->pending);
}

static void
workspace_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_workspace *workspace = wl_container_of(listener, workspace, transaction_apply);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

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
    workspace->pending.columns = create_list();
    workspace->committed.floating = create_list();
    workspace->committed.columns = create_list();
    workspace->current.floating = create_list();
    workspace->current.columns = create_list();

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
    list_free(workspace->pending.columns);
    list_free(workspace->committed.floating);
    list_free(workspace->committed.columns);
    list_free(workspace->current.floating);
    list_free(workspace->current.columns);
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

    if (workspace->pending.columns->length) {
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
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

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
    for (int i = 0; i < workspace->committed.columns->length; i++) {
        struct hwd_column *column = workspace->committed.columns->items[i];
        column_set_dirty(column);
    }

    for (int i = 0; i < workspace->pending.floating->length; i++) {
        struct hwd_window *window = workspace->pending.floating->items[i];
        window_set_dirty(window);
    }

    for (int i = 0; i < workspace->pending.columns->length; i++) {
        struct hwd_column *column = workspace->pending.columns->items[i];
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

    if (!dirty) {
        return;
    }

    for (int column_index = 0; column_index < workspace->pending.columns->length; column_index++) {
        struct hwd_column *column = workspace->pending.columns->items[column_index];
        column_reconcile(column, workspace, column->pending.output);
    }

    for (int window_index = 0; window_index < workspace->pending.floating->length; window_index++) {
        struct hwd_window *window = workspace->pending.floating->items[window_index];
        window_reconcile_floating(window, workspace);
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
        for (int column_index = 0; column_index < workspace->pending.columns->length;
             column_index++) {
            struct hwd_column *column = workspace->pending.columns->items[column_index];
            column_reconcile(column, workspace, column->pending.output);
        }

        for (int window_index = 0; window_index < workspace->pending.floating->length;
             window_index++) {
            struct hwd_window *window = workspace->pending.floating->items[window_index];
            window_reconcile_floating(window, workspace);
        }
    }
}

static void
arrange_floating(struct hwd_workspace *workspace) {
    list_t *floating = workspace->pending.floating;
    for (int i = 0; i < floating->length; ++i) {
        struct hwd_window *floater = floating->items[i];
        floater->pending.shaded = false;
        window_arrange(floater);
    }
}

static void
arrange_tiling(struct hwd_workspace *workspace) {
    list_t *columns = workspace->pending.columns;
    if (!columns->length) {
        return;
    }

    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];

        struct wlr_box box;
        output_get_usable_area(output, &box);

        // Count the number of new columns we are resizing, and how much space
        // is currently occupied.
        int new_columns = 0;
        int total_columns = 0;
        double current_width_fraction = 0;
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            if (column->pending.output != output) {
                continue;
            }

            current_width_fraction += column->width_fraction;
            if (column->width_fraction <= 0) {
                new_columns += 1;
            }
            total_columns += 1;
        }

        // Calculate each width fraction.
        double total_width_fraction = 0;
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            if (column->pending.output != output) {
                continue;
            }

            if (column->width_fraction <= 0) {
                if (current_width_fraction <= 0) {
                    column->width_fraction = 1.0;
                } else if (total_columns > new_columns) {
                    column->width_fraction = current_width_fraction / (total_columns - new_columns);
                } else {
                    column->width_fraction = current_width_fraction;
                }
            }
            total_width_fraction += column->width_fraction;
        }
        // Normalize width fractions so the sum is 1.0.
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            if (column->pending.output != output) {
                continue;
            }
            column->width_fraction /= total_width_fraction;
        }

        double columns_total_width = box.width;

        // Resize columns.
        double column_x = box.x;
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            column->child_total_width = columns_total_width;
            column->pending.x = column_x;
            column->pending.y = box.y;
            column->pending.width = round(column->width_fraction * columns_total_width);
            column->pending.height = box.height;
            column_x += column->pending.width;

            // Make last child use remaining width of parent.
            if (j == total_columns - 1) {
                column->pending.width = box.x + box.width - column->pending.x;
            }
        }
    }

    for (int i = 0; i < columns->length; ++i) {
        struct hwd_column *column = columns->items[i];
        column_arrange(column);
    }
}

void
workspace_arrange(struct hwd_workspace *workspace) {
    if (config->reloading) {
        return;
    }

    hwd_log(HWD_DEBUG, "Arranging workspace '%s'", workspace->name);

    arrange_tiling(workspace);
    arrange_floating(workspace);

    workspace_set_dirty(workspace);
}

void
workspace_add_floating(struct hwd_workspace *workspace, struct hwd_window *window) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(window->pending.parent == NULL, "Window still has a parent");
    hwd_assert(window->pending.workspace == NULL, "Window is already attached to a workspace");

    struct hwd_window *prev_active_floating = workspace_get_active_floating_window(workspace);

    list_add(workspace->pending.floating, window);

    if (window->pending.output == NULL) {
        window->pending.output = root_get_active_output(workspace->pending.root);
    }

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

struct hwd_column *
workspace_get_column_first(struct hwd_workspace *workspace, struct hwd_output *output) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(output != NULL, "Expected output");

    for (int i = 0; i < workspace->pending.columns->length; i++) {
        struct hwd_column *column = workspace->pending.columns->items[i];
        if (column->pending.output == output) {
            return column;
        }
    }
    return NULL;
}

struct hwd_column *
workspace_get_column_last(struct hwd_workspace *workspace, struct hwd_output *output) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(output != NULL, "Expected output");

    for (int i = workspace->pending.columns->length - 1; i >= 0; i--) {
        struct hwd_column *column = workspace->pending.columns->items[i];
        if (column->pending.output == output) {
            return column;
        }
    }
    return NULL;
}

struct hwd_column *
workspace_get_column_before(struct hwd_workspace *workspace, struct hwd_column *column) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == workspace, "Expected column to be part of workspace");

    int i = workspace->pending.columns->length - 1;
    for (; i >= 0; i--) {
        struct hwd_column *candidate_column = workspace->pending.columns->items[i];
        if (candidate_column == column) {
            break;
        }
    }
    hwd_assert(i != -1, "Could not find column in workspace");
    i--;

    struct hwd_output *output = column->pending.output;
    for (; i >= 0; i--) {
        struct hwd_column *candidate_column = workspace->pending.columns->items[i];
        if (candidate_column->pending.output == output) {
            return candidate_column;
        }
    }

    return NULL;
}

struct hwd_column *
workspace_get_column_after(struct hwd_workspace *workspace, struct hwd_column *column) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == workspace, "Expected column to be part of workspace");

    int i = 0;
    for (; i < workspace->pending.columns->length; i++) {
        struct hwd_column *candidate_column = workspace->pending.columns->items[i];
        if (candidate_column == column) {
            break;
        }
    }
    hwd_assert(i != workspace->pending.columns->length, "Could not find column in workspace");
    i++;

    struct hwd_output *output = column->pending.output;
    for (; i < workspace->pending.columns->length; i++) {
        struct hwd_column *candidate_column = workspace->pending.columns->items[i];
        if (candidate_column->pending.output == output) {
            return candidate_column;
        }
    }

    return NULL;
}

void
workspace_insert_column_first(
    struct hwd_workspace *workspace, struct hwd_output *output, struct hwd_column *column
) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(output != NULL, "Expected output");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == NULL, "Column is already attached to a workspace");
    hwd_assert(column->pending.output == NULL, "Column is already attached to an output");

    list_t *columns = workspace->pending.columns;
    list_insert(columns, 0, column);

    column_reconcile(column, workspace, output);
    workspace_set_dirty(workspace);
}

void
workspace_insert_column_last(
    struct hwd_workspace *workspace, struct hwd_output *output, struct hwd_column *column
) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(output != NULL, "Expected output");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == NULL, "Column is already attached to a workspace");
    hwd_assert(column->pending.output == NULL, "Column is already attached to an output");

    list_t *columns = workspace->pending.columns;
    list_insert(columns, columns->length, column);

    column_reconcile(column, workspace, output);
    workspace_set_dirty(workspace);
}

void
workspace_insert_column_before(
    struct hwd_workspace *workspace, struct hwd_column *fixed, struct hwd_column *column
) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(fixed != NULL, "Expected sibling column");
    hwd_assert(fixed->pending.workspace != NULL, "Expected sibling column attached to workspace");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == NULL, "Column is already attached to a workspace");
    hwd_assert(column->pending.output == NULL, "Column is already attached to an output");

    list_t *columns = workspace->pending.columns;
    int index = list_find(columns, fixed);
    hwd_assert(index != -1, "Could not find sibling column in columns array");
    list_insert(columns, index, column);

    column_reconcile(column, workspace, fixed->pending.output);
    workspace_set_dirty(workspace);
}

void
workspace_insert_column_after(
    struct hwd_workspace *workspace, struct hwd_column *fixed, struct hwd_column *column
) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(fixed != NULL, "Expected sibling column");
    hwd_assert(fixed->pending.workspace != NULL, "Expected sibling column attached to workspace");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == NULL, "Column is already attached to a workspace");
    hwd_assert(column->pending.output == NULL, "Column is already attached to an output");

    list_t *columns = workspace->pending.columns;
    int index = list_find(columns, fixed);
    hwd_assert(index != -1, "Could not find sibling column in columns array");
    list_insert(columns, index + 1, column);

    column_reconcile(column, workspace, fixed->pending.output);
    workspace_set_dirty(workspace);
}

void
workspace_remove_tiling(struct hwd_workspace *workspace, struct hwd_column *column) {
    hwd_assert(workspace != NULL, "Expected workspace");
    hwd_assert(column != NULL, "Expected column");
    hwd_assert(column->pending.workspace == workspace, "Column is not a child of workspace");

    struct hwd_output *output = column->pending.output;
    hwd_assert(output != NULL, "Expected output");

    int index = list_find(workspace->pending.columns, column);
    hwd_assert(index != -1, "Column is missing from workspace column list");

    list_del(workspace->pending.columns, index);

    if (workspace->pending.active_column == column) {
        struct hwd_column *next_active = NULL;

        for (int candidate_index = 0; candidate_index < workspace->pending.columns->length;
             candidate_index++) {
            struct hwd_column *candidate = workspace->pending.columns->items[candidate_index];

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

struct hwd_column *
workspace_get_column_at(struct hwd_workspace *workspace, double x, double y) {
    for (int i = 0; i < workspace->pending.columns->length; i++) {
        struct hwd_column *column = workspace->pending.columns->items[i];

        struct wlr_box column_box;
        column_get_box(column, &column_box);
        if (wlr_box_contains_point(&column_box, x, y)) {
            return column;
        }
    }
    return NULL;
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

    return workspace->pending.floating->items[workspace->pending.floating->length - 1];
}

static struct hwd_window *
workspace_get_committed_active_floating_window(struct hwd_workspace *workspace) {
    if (workspace->committed.floating->length == 0) {
        return NULL;
    }

    return workspace->committed.floating->items[workspace->committed.floating->length - 1];
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

    workspace_arrange(workspace);
}

struct hwd_window *
workspace_get_floating_window_at(struct hwd_workspace *workspace, double x, double y) {
    for (int i = workspace->pending.floating->length - 1; i >= 0; i--) {
        struct hwd_window *window = workspace->pending.floating->items[i];

        if (window->moving) {
            continue;
        }

        struct wlr_box window_box;
        window_get_box(window, &window_box);
        if (wlr_box_contains_point(&window_box, x, y)) {
            return window;
        }
    }
    return NULL;
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
    for (int i = 0; i < workspace->pending.columns->length; ++i) {
        struct hwd_column *child = workspace->pending.columns->items[i];
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
