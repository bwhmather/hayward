#define _POSIX_C_SOURCE 200809
#include "hayward/tree/workspace.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "hayward-common/list.h"
#include "hayward-common/log.h"
#include "hayward-common/stringop.h"
#include "hayward-common/util.h"

#include "hayward/input/cursor.h"
#include "hayward/input/input-manager.h"
#include "hayward/input/seat.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/column.h"
#include "hayward/tree/node.h"
#include "hayward/tree/view.h"
#include "hayward/tree/window.h"

struct workspace_config *workspace_find_config(const char *workspace_name) {
    for (int i = 0; i < config->workspace_configs->length; ++i) {
        struct workspace_config *wsc = config->workspace_configs->items[i];
        if (strcmp(wsc->workspace, workspace_name) == 0) {
            return wsc;
        }
    }
    return NULL;
}

struct hayward_workspace *workspace_create(const char *name) {
    struct hayward_workspace *workspace =
        calloc(1, sizeof(struct hayward_workspace));
    if (!workspace) {
        hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_workspace");
        return NULL;
    }
    node_init(&workspace->node, N_WORKSPACE, workspace);
    workspace->name = name ? strdup(name) : NULL;
    workspace->pending.floating = create_list();
    workspace->pending.tiling = create_list();

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
    wl_signal_emit(&root->events.new_node, &workspace->node);

    return workspace;
}

void workspace_destroy(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(
        workspace->node.destroying,
        "Tried to free workspace which wasn't marked as destroying"
    );
    hayward_assert(
        workspace->node.ntxnrefs == 0,
        "Tried to free workspace "
        "which is still referenced by transactions"
    );

    free(workspace->name);
    list_free_items_and_destroy(workspace->output_priority);
    list_free(workspace->pending.floating);
    list_free(workspace->pending.tiling);
    list_free(workspace->current.floating);
    list_free(workspace->current.tiling);
    free(workspace);
}

void workspace_begin_destroy(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_log(HAYWARD_DEBUG, "Destroying workspace '%s'", workspace->name);
    ipc_event_workspace(NULL, workspace, "empty"); // intentional
    wl_signal_emit(&workspace->node.events.destroy, &workspace->node);

    workspace_detach(workspace);

    workspace->node.destroying = true;
    node_set_dirty(&workspace->node);
}

void workspace_consider_destroy(struct hayward_workspace *workspace) {
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

static bool
_workspace_by_name(struct hayward_workspace *workspace, void *data) {
    return strcasecmp(workspace->name, data) == 0;
}

struct hayward_workspace *workspace_by_name(const char *name) {
    return root_find_workspace(_workspace_by_name, (void *)name);
}

bool workspace_is_visible(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->node.destroying) {
        return false;
    }

    return root_get_active_workspace() == workspace;
}

bool workspace_is_empty(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace->pending.tiling->length) {
        return false;
    }
    // Sticky views are not considered to be part of this workspace
    for (int i = 0; i < workspace->pending.floating->length; ++i) {
        struct hayward_window *floater = workspace->pending.floating->items[i];
        if (!window_is_sticky(floater)) {
            return false;
        }
    }
    return true;
}

static int find_output(const void *id1, const void *id2) {
    return strcmp(id1, id2);
}

static int workspace_output_get_priority(
    struct hayward_workspace *workspace, struct hayward_output *output
) {
    char identifier[128];
    output_get_identifier(identifier, sizeof(identifier), output);
    int index_id =
        list_seq_find(workspace->output_priority, find_output, identifier);
    int index_name = list_seq_find(
        workspace->output_priority, find_output, output->wlr_output->name
    );
    return index_name < 0 || index_id < index_name ? index_id : index_name;
}

void workspace_output_raise_priority(
    struct hayward_workspace *workspace, struct hayward_output *old_output,
    struct hayward_output *output
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    int old_index = workspace_output_get_priority(workspace, old_output);
    if (old_index < 0) {
        return;
    }

    int new_index = workspace_output_get_priority(workspace, output);
    if (new_index < 0) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        list_insert(workspace->output_priority, old_index, strdup(identifier));
    } else if (new_index > old_index) {
        char *name = workspace->output_priority->items[new_index];
        list_del(workspace->output_priority, new_index);
        list_insert(workspace->output_priority, old_index, name);
    }
}

void workspace_output_add_priority(
    struct hayward_workspace *workspace, struct hayward_output *output
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace_output_get_priority(workspace, output) < 0) {
        char identifier[128];
        output_get_identifier(identifier, sizeof(identifier), output);
        list_add(workspace->output_priority, strdup(identifier));
    }
}

struct hayward_output *workspace_output_get_highest_available(
    struct hayward_workspace *workspace, struct hayward_output *exclude
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    char exclude_id[128] = {'\0'};
    if (exclude) {
        output_get_identifier(exclude_id, sizeof(exclude_id), exclude);
    }

    for (int i = 0; i < workspace->output_priority->length; i++) {
        char *name = workspace->output_priority->items[i];
        if (exclude &&
            (strcmp(name, exclude->wlr_output->name) == 0 ||
             strcmp(name, exclude_id) == 0)) {
            continue;
        }

        struct hayward_output *output = output_by_name_or_id(name);
        if (output) {
            return output;
        }
    }

    return NULL;
}

static bool find_urgent_iterator(struct hayward_window *container, void *data) {
    return container->view && view_is_urgent(container->view);
}

void workspace_detect_urgent(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    bool new_urgent =
        (bool)workspace_find_window(workspace, find_urgent_iterator, NULL);

    if (workspace->urgent != new_urgent) {
        workspace->urgent = new_urgent;
        ipc_event_workspace(NULL, workspace, "urgent");
        workspace_damage_whole(workspace);
    }
}

void workspace_detach(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    root_remove_workspace(workspace);
}

void workspace_reconcile(struct hayward_workspace *workspace) {
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

void workspace_reconcile_detached(struct hayward_workspace *workspace) {
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

void workspace_add_floating(
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
        node_set_dirty(&prev_active_floating->node);
    }

    node_set_dirty(&workspace->node);
    node_set_dirty(&window->node);
}

void workspace_remove_floating(
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

void workspace_insert_tiling(
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

    node_set_dirty(&workspace->node);
    node_set_dirty(&column->node);
}

void workspace_remove_tiling(
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

            node_set_dirty(&next_active->node);
        }
    }

    column_reconcile_detached(column);

    node_set_dirty(&workspace->node);
    node_set_dirty(&column->node);
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

void workspace_add_gaps(struct hayward_workspace *workspace) {
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

void workspace_get_box(
    struct hayward_workspace *workspace, struct wlr_box *box
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(box != NULL, "Expected box");

    box->x = workspace->pending.x;
    box->y = workspace->pending.y;
    box->width = workspace->pending.width;
    box->height = workspace->pending.height;
}

static void count_tiling_views(struct hayward_window *container, void *data) {
    if (!window_is_floating(container)) {
        size_t *count = data;
        *count += 1;
    }
}

size_t workspace_num_tiling_views(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    size_t count = 0;
    workspace_for_each_window(workspace, count_tiling_views, &count);
    return count;
}

static void
count_sticky_containers(struct hayward_window *container, void *data) {
    if (!window_is_sticky(container)) {
        return;
    }

    size_t *count = data;
    *count += 1;
}

size_t workspace_num_sticky_containers(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    size_t count = 0;
    workspace_for_each_window(workspace, count_sticky_containers, &count);
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

void workspace_set_active_window(
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

struct hayward_window *workspace_get_fullscreen_window_for_output(
    struct hayward_workspace *workspace, struct hayward_output *output
) {
    hayward_assert(workspace != NULL, "Expected workspace");
    hayward_assert(output != NULL, "Expected output");

    return workspace_find_window(
        workspace, is_fullscreen_window_for_output, output
    );
}

void workspace_for_each_window(
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

void workspace_for_each_column(
    struct hayward_workspace *workspace,
    void (*f)(struct hayward_column *column, void *data), void *data
) {
    hayward_assert(workspace != NULL, "Expected workspace");

    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hayward_column *column = workspace->pending.tiling->items[i];
        f(column, data);
    }
}

struct hayward_window *workspace_find_window(
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

void workspace_damage_whole(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    if (!workspace_is_visible(workspace)) {
        return;
    }

    for (int i = 0; i < root->outputs->length; i++) {
        struct hayward_output *output = root->outputs->items[i];
        output_damage_whole(output);
    }
}
