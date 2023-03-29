#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/workspace.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/desktop/transaction.h>
#include <hayward/ipc-server.h>
#include <hayward/output.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#include <config.h>

static void
workspace_destroy(struct hayward_workspace *workspace);

static void
workspace_copy_state(
    struct hayward_workspace_state *tgt, struct hayward_workspace_state *src
) {
    list_t *tgt_floating = tgt->floating;
    list_t *tgt_tiling = tgt->tiling;

    memcpy(tgt, src, sizeof(struct hayward_workspace_state));

    tgt->floating = tgt_floating;
    list_clear(tgt->floating);
    list_cat(tgt->floating, src->floating);

    tgt->tiling = tgt_tiling;
    list_clear(tgt->tiling);
    list_cat(tgt->tiling, src->tiling);
}

static void
workspace_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hayward_workspace *workspace =
        wl_container_of(listener, workspace, transaction_commit);

    wl_list_remove(&listener->link);
    workspace->dirty = false;

    transaction_add_apply_listener(&workspace->transaction_apply);

    workspace_copy_state(&workspace->committed, &workspace->pending);
}

static void
workspace_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hayward_workspace *workspace =
        wl_container_of(listener, workspace, transaction_apply);

    wl_list_remove(&listener->link);

    // Damage the old location
    workspace_damage_whole(workspace);

    workspace_copy_state(&workspace->current, &workspace->committed);

    // Damage the new location
    workspace_damage_whole(workspace);

    if (workspace->current.dead) {
        workspace_destroy(workspace);
    }
}

struct workspace_config *
workspace_find_config(const char *workspace_name) {
    for (int i = 0; i < config->workspace_configs->length; ++i) {
        struct workspace_config *wsc = config->workspace_configs->items[i];
        if (strcmp(wsc->workspace, workspace_name) == 0) {
            return wsc;
        }
    }
    return NULL;
}

struct hayward_workspace *
workspace_create(const char *name) {
    struct hayward_workspace *workspace =
        calloc(1, sizeof(struct hayward_workspace));
    if (!workspace) {
        hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_workspace");
        return NULL;
    }

    static size_t next_id = 1;
    workspace->id = next_id++;

    wl_signal_init(&workspace->events.begin_destroy);

    workspace->transaction_commit.notify = workspace_handle_transaction_commit;
    workspace->transaction_apply.notify = workspace_handle_transaction_apply;

    workspace->name = name ? strdup(name) : NULL;

    workspace->pending.floating = create_list();
    workspace->pending.tiling = create_list();
    workspace->committed.floating = create_list();
    workspace->committed.tiling = create_list();
    workspace->current.floating = create_list();
    workspace->current.tiling = create_list();

    workspace->gaps_outer = config->gaps_outer;
    workspace->gaps_inner = config->gaps_inner;
    if (name) {
        struct workspace_config *wsc = workspace_find_config(name);
        if (wsc) {
            if (wsc->gaps_outer.top != INT_MIN) {
                workspace->gaps_outer.top = wsc->gaps_outer.top;
            }
            if (wsc->gaps_outer.right != INT_MIN) {
                workspace->gaps_outer.right = wsc->gaps_outer.right;
            }
            if (wsc->gaps_outer.bottom != INT_MIN) {
                workspace->gaps_outer.bottom = wsc->gaps_outer.bottom;
            }
            if (wsc->gaps_outer.left != INT_MIN) {
                workspace->gaps_outer.left = wsc->gaps_outer.left;
            }
            if (wsc->gaps_inner != INT_MIN) {
                workspace->gaps_inner = wsc->gaps_inner;
            }
        }
    }

    root_add_workspace(workspace);
    root_sort_workspaces();

    ipc_event_workspace(NULL, workspace, "init");

    return workspace;
}

bool
workspace_is_alive(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");
    return !workspace->pending.dead;
}

static void
workspace_destroy(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(
        workspace->current.dead,
        "Tried to free workspace which wasn't marked as destroying"
    );
    hayward_assert(
        !workspace->dirty,
        "Tried to free workspace which is queued for the next transaction"
    );

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
workspace_begin_destroy(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(workspace_is_alive(workspace), "Expected live workspace");

    hayward_log(HAYWARD_DEBUG, "Destroying workspace '%s'", workspace->name);

    workspace->pending.dead = true;

    ipc_event_workspace(NULL, workspace, "empty"); // intentional

    workspace_detach(workspace);

    wl_signal_emit(&workspace->events.begin_destroy, workspace);

    workspace_set_dirty(workspace);
}

void
workspace_consider_destroy(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.tiling->length ||
        workspace->pending.floating->length) {
        return;
    }

    if (root_get_active_workspace() == workspace) {
        return;
    }

    workspace_begin_destroy(workspace);
}

void
workspace_set_dirty(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->dirty) {
        return;
    }

    workspace->dirty = true;
    transaction_add_commit_listener(&workspace->transaction_commit);
    transaction_ensure_queued();

    for (int i = 0; i < workspace->committed.floating->length; i++) {
        struct hayward_window *window = workspace->committed.floating->items[i];
        window_set_dirty(window);
    }
    for (int i = 0; i < workspace->committed.tiling->length; i++) {
        struct hayward_column *column = workspace->committed.tiling->items[i];
        column_set_dirty(column);
    }

    for (int i = 0; i < workspace->pending.floating->length; i++) {
        struct hayward_window *window = workspace->pending.floating->items[i];
        window_set_dirty(window);
    }

    for (int i = 0; i < workspace->pending.tiling->length; i++) {
        struct hayward_column *column = workspace->pending.tiling->items[i];
        column_set_dirty(column);
    }
}

static bool
_workspace_by_name(struct hayward_workspace *workspace, void *data) {
    return strcasecmp(workspace->name, data) == 0;
}

struct hayward_workspace *
workspace_by_name(const char *name) {
    return root_find_workspace(_workspace_by_name, (void *)name);
}

bool
workspace_is_visible(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.dead) {
        return false;
    }

    return root_get_active_workspace() == workspace;
}

bool
workspace_is_empty(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.tiling->length) {
        return false;
    }

    if (workspace->pending.floating->length) {
        return false;
    }

    return true;
}

static bool
find_urgent_iterator(struct hayward_window *window, void *data) {
    return window->view && view_is_urgent(window->view);
}

void
workspace_detect_urgent(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    bool new_urgent =
        (bool)workspace_find_window(workspace, find_urgent_iterator, NULL);

    if (workspace->urgent != new_urgent) {
        workspace->urgent = new_urgent;
        ipc_event_workspace(NULL, workspace, "urgent");
        workspace_damage_whole(workspace);
    }
}

void
workspace_detach(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    root_remove_workspace(workspace);
}

void
workspace_reconcile(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    bool dirty = false;

    bool should_focus = workspace == root_get_active_workspace();
    if (should_focus != workspace->pending.focused) {
        workspace->pending.focused = should_focus;
        dirty = true;
    }

    if (dirty) {
        for (int column_index = 0;
             column_index < workspace->pending.tiling->length; column_index++) {
            struct hayward_column *column =
                workspace->pending.tiling->items[column_index];
            column_reconcile(column, workspace, column->pending.output);
        }

        for (int window_index = 0;
             window_index < workspace->pending.floating->length;
             window_index++) {
            struct hayward_window *window =
                workspace->pending.floating->items[window_index];
            window_reconcile_floating(window, workspace);
        }
    }
}

void
workspace_reconcile_detached(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    bool dirty = false;

    bool should_focus = false;
    if (should_focus != workspace->pending.focused) {
        workspace->pending.focused = should_focus;
        dirty = true;
    }

    if (dirty) {
        for (int column_index = 0;
             column_index < workspace->pending.tiling->length; column_index++) {
            struct hayward_column *column =
                workspace->pending.tiling->items[column_index];
            column_reconcile(column, workspace, column->pending.output);
        }

        for (int window_index = 0;
             window_index < workspace->pending.floating->length;
             window_index++) {
            struct hayward_window *window =
                workspace->pending.floating->items[window_index];
            window_reconcile_floating(window, workspace);
        }
    }
}

void
workspace_add_floating(
    struct hayward_workspace *workspace, struct hayward_window *window
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window->pending.parent == NULL, "Window still has a parent");
    hayward_assert(
        window->pending.workspace == NULL,
        "Window is already attached to a workspace"
    );

    struct hayward_window *prev_active_floating =
        workspace_get_active_floating_window(workspace);

    list_add(workspace->pending.floating, window);

    window_reconcile_floating(window, workspace);

    if (prev_active_floating) {
        window_reconcile_floating(prev_active_floating, workspace);
    }

    workspace_set_dirty(workspace);
}

void
workspace_remove_floating(
    struct hayward_workspace *workspace, struct hayward_window *window
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(
        window->pending.workspace == workspace,
        "Window is not a child of workspace"
    );
    hayward_assert(window->pending.parent == NULL, "Window is not floating");

    int index = list_find(workspace->pending.floating, window);
    hayward_assert(index != -1, "Window missing from floating list");

    list_del(workspace->pending.floating, index);

    if (workspace->pending.floating->length == 0) {
        // Switch back to tiling mode.
        workspace->pending.focus_mode = F_TILING;

        struct hayward_window *next_active =
            workspace_get_active_tiling_window(workspace);
        if (next_active != NULL) {
            window_reconcile_tiling(next_active, next_active->pending.parent);
        }
    } else {
        // Focus next floating window.
        window_reconcile_floating(
            workspace_get_active_floating_window(workspace), workspace
        );
    }

    if (window->pending.output && window->pending.fullscreen) {
        output_reconcile(window->pending.output);
    }

    window_reconcile_detached(window);
}

void
workspace_insert_tiling(
    struct hayward_workspace *workspace, struct hayward_output *output,
    struct hayward_column *column, int index
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(output != NULL, "Expected output");
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(
        column->pending.workspace == NULL,
        "Column is already attached to a workspace"
    );
    hayward_assert(
        column->pending.output == NULL,
        "Column is already attached to an output"
    );
    hayward_assert(
        index >= 0 && index <= workspace->pending.tiling->length,
        "Column index not in bounds"
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
workspace_remove_tiling(
    struct hayward_workspace *workspace, struct hayward_column *column
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(column != NULL, "Expected column");
    hayward_assert(
        column->pending.workspace == workspace,
        "Column is not a child of workspace"
    );

    struct hayward_output *output = column->pending.output;
    hayward_assert(output != NULL, "Expected output");

    int index = list_find(workspace->pending.tiling, column);
    hayward_assert(index != -1, "Column is missing from workspace column list");

    list_del(workspace->pending.tiling, index);

    if (workspace->pending.active_column == column) {
        struct hayward_column *next_active = NULL;

        for (int candidate_index = 0;
             candidate_index < workspace->pending.tiling->length;
             candidate_index++) {
            struct hayward_column *candidate =
                workspace->pending.tiling->items[candidate_index];

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

static bool
workspace_has_single_visible_container(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.tiling->length != 1) {
        return false;
    }

    struct hayward_column *column = workspace->pending.tiling->items[0];
    if (column->pending.layout == L_STACKED) {
        return true;
    }

    if (column->pending.children->length == 1) {
        return true;
    }

    return false;
}

void
workspace_add_gaps(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (config->smart_gaps == SMART_GAPS_ON &&
        workspace_has_single_visible_container(workspace)) {
        workspace->current_gaps.top = 0;
        workspace->current_gaps.right = 0;
        workspace->current_gaps.bottom = 0;
        workspace->current_gaps.left = 0;
        return;
    }

    if (config->smart_gaps == SMART_GAPS_INVERSE_OUTER &&
        !workspace_has_single_visible_container(workspace)) {
        workspace->current_gaps.top = 0;
        workspace->current_gaps.right = 0;
        workspace->current_gaps.bottom = 0;
        workspace->current_gaps.left = 0;
    } else {
        workspace->current_gaps = workspace->gaps_outer;
    }

    // Add inner gaps and make sure we don't turn out negative
    workspace->current_gaps.top =
        fmax(0, workspace->current_gaps.top + workspace->gaps_inner);
    workspace->current_gaps.right =
        fmax(0, workspace->current_gaps.right + workspace->gaps_inner);
    workspace->current_gaps.bottom =
        fmax(0, workspace->current_gaps.bottom + workspace->gaps_inner);
    workspace->current_gaps.left =
        fmax(0, workspace->current_gaps.left + workspace->gaps_inner);

    // Now that we have the total gaps calculated we may need to clamp them in
    // case they've made the available area too small
    if (workspace->pending.width - workspace->current_gaps.left -
                workspace->current_gaps.right <
            MIN_SANE_W &&
        workspace->current_gaps.left + workspace->current_gaps.right > 0) {
        int total_gap = fmax(0, workspace->pending.width - MIN_SANE_W);
        double left_gap_frac =
            ((double)workspace->current_gaps.left /
             ((double)workspace->current_gaps.left +
              (double)workspace->current_gaps.right));
        workspace->current_gaps.left = left_gap_frac * total_gap;
        workspace->current_gaps.right =
            total_gap - workspace->current_gaps.left;
    }
    if (workspace->pending.height - workspace->current_gaps.top -
                workspace->current_gaps.bottom <
            MIN_SANE_H &&
        workspace->current_gaps.top + workspace->current_gaps.bottom > 0) {
        int total_gap = fmax(0, workspace->pending.height - MIN_SANE_H);
        double top_gap_frac =
            ((double)workspace->current_gaps.top /
             ((double)workspace->current_gaps.top +
              (double)workspace->current_gaps.bottom));
        workspace->current_gaps.top = top_gap_frac * total_gap;
        workspace->current_gaps.bottom =
            total_gap - workspace->current_gaps.top;
    }

    workspace->pending.x += workspace->current_gaps.left;
    workspace->pending.y += workspace->current_gaps.top;
    workspace->pending.width -=
        workspace->current_gaps.left + workspace->current_gaps.right;
    workspace->pending.height -=
        workspace->current_gaps.top + workspace->current_gaps.bottom;
}

void
workspace_get_box(struct hayward_workspace *workspace, struct wlr_box *box) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(box != NULL, "Expected box");

    box->x = workspace->pending.x;
    box->y = workspace->pending.y;
    box->width = workspace->pending.width;
    box->height = workspace->pending.height;
}

static void
count_tiling_views(struct hayward_window *window, void *data) {
    if (!window_is_floating(window)) {
        size_t *count = data;
        *count += 1;
    }
}

size_t
workspace_num_tiling_views(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    size_t count = 0;
    workspace_for_each_window(workspace, count_tiling_views, &count);
    return count;
}

struct hayward_output *
workspace_get_active_output(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    struct hayward_column *active_column = workspace->pending.active_column;
    if (active_column != NULL) {
        return active_column->pending.output;
    }

    return NULL;
}

struct hayward_output *
workspace_get_current_active_output(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    struct hayward_column *active_column = workspace->current.active_column;

    if (active_column != NULL) {
        return active_column->current.output;
    }

    return NULL;
}

struct hayward_window *
workspace_get_active_tiling_window(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    struct hayward_column *active_column = workspace->pending.active_column;
    if (active_column == NULL) {
        return NULL;
    }

    return active_column->pending.active_child;
}

struct hayward_window *
workspace_get_active_floating_window(struct hayward_workspace *workspace) {
    if (workspace->pending.floating->length == 0) {
        return NULL;
    }

    return workspace->pending.floating->items[0];
}

struct hayward_window *
workspace_get_active_window(struct hayward_workspace *workspace) {
    switch (workspace->pending.focus_mode) {
    case F_TILING:
        return workspace_get_active_tiling_window(workspace);
    case F_FLOATING:
        return workspace_get_active_floating_window(workspace);
    default:
        hayward_abort("Invalid focus mode");
    }
}

void
workspace_set_active_window(
    struct hayward_workspace *workspace, struct hayward_window *window
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    struct hayward_window *prev_active = workspace_get_active_window(workspace);
    if (window == prev_active) {
        return;
    }

    if (window == NULL) {
        workspace->pending.active_column = NULL;
        workspace->pending.focus_mode = F_TILING;
    } else if (window_is_floating(window)) {
        hayward_assert(
            window->pending.workspace == workspace,
            "Window attached to wrong workspace"
        );

        int index = list_find(workspace->pending.floating, window);
        hayward_assert(
            index != -1, "Window missing from list of floating windows"
        );

        list_del(workspace->pending.floating, index);
        list_add(workspace->pending.floating, window);

        workspace->pending.focus_mode = F_FLOATING;

        window_reconcile_floating(window, workspace);
    } else {
        hayward_assert(
            window->pending.workspace == workspace,
            "Window attached to wrong workspace"
        );

        struct hayward_column *old_column = workspace->pending.active_column;
        struct hayward_column *new_column = window->pending.parent;
        hayward_assert(
            new_column->pending.workspace == workspace,
            "Column attached to wrong workspace"
        );

        column_set_active_child(new_column, window);

        workspace->pending.active_column = new_column;
        workspace->pending.focus_mode = F_TILING;

        if (root_get_active_workspace() == workspace) {
            root_set_active_output(new_column->pending.output);
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
is_fullscreen_window_for_output(struct hayward_window *window, void *data) {
    struct hayward_output *output = data;

    if (window->pending.output != output) {
        return false;
    }

    if (!window->pending.fullscreen) {
        return false;
    }

    return true;
}

struct hayward_window *
workspace_get_fullscreen_window_for_output(
    struct hayward_workspace *workspace, struct hayward_output *output
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(output != NULL, "Expected output");

    return workspace_find_window(
        workspace, is_fullscreen_window_for_output, output
    );
}

void
workspace_for_each_window(
    struct hayward_workspace *workspace,
    void (*f)(struct hayward_window *window, void *data), void *data
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    // Tiling
    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hayward_column *column = workspace->pending.tiling->items[i];
        column_for_each_child(column, f, data);
    }
    // Floating
    for (int i = 0; i < workspace->pending.floating->length; ++i) {
        struct hayward_window *window = workspace->pending.floating->items[i];
        f(window, data);
    }
}

void
workspace_for_each_column(
    struct hayward_workspace *workspace,
    void (*f)(struct hayward_column *column, void *data), void *data
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hayward_column *column = workspace->pending.tiling->items[i];
        f(column, data);
    }
}

struct hayward_window *
workspace_find_window(
    struct hayward_workspace *workspace,
    bool (*test)(struct hayward_window *window, void *data), void *data
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    struct hayward_window *result = NULL;
    // Tiling
    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hayward_column *child = workspace->pending.tiling->items[i];
        if ((result = column_find_child(child, test, data))) {
            return result;
        }
    }
    // Floating
    for (int i = 0; i < workspace->pending.floating->length; ++i) {
        struct hayward_window *child = workspace->pending.floating->items[i];
        if (test(child, data)) {
            return child;
        }
    }
    return NULL;
}

void
workspace_damage_whole(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (!workspace_is_visible(workspace)) {
        return;
    }

    for (int i = 0; i < root->outputs->length; i++) {
        struct hayward_output *output = root->outputs->items[i];
        output_damage_whole(output);
    }
}
