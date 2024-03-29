#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/tree/root.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/desktop/hwd_workspace_management_v1.h>
#include <hayward/desktop/idle_inhibit_v1.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/server.h>
#include <hayward/theme.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

struct hwd_root *root;

struct hwd_pid_workspace {
    pid_t pid;
    char *workspace_name;
    struct timespec time_added;

    struct wl_list link;
};

static void
root_validate(struct hwd_root *root);

static void
root_init_scene(struct hwd_root *root) {
    root->root_scene = wlr_scene_create();

    root->layers.background = wlr_scene_tree_create(&root->root_scene->tree);
    root->layers.workspaces = wlr_scene_tree_create(&root->root_scene->tree);
    root->layers.unmanaged = wlr_scene_tree_create(&root->root_scene->tree);
    root->layers.moving = wlr_scene_tree_create(&root->root_scene->tree);
    root->layers.overlay = wlr_scene_tree_create(&root->root_scene->tree);
    root->layers.popups = wlr_scene_tree_create(&root->root_scene->tree);
}

static void
root_update_layer_workspaces(struct hwd_root *root) {
    struct wl_list *link = &root->layers.workspaces->children;

    if (root->committed.workspaces->length) {
        // Anchor top most workspace at top of stack.
        list_t *workspaces = root->committed.workspaces;
        int workspace_index = workspaces->length - 1;

        struct hwd_workspace *workspace = workspaces->items[workspace_index];
        wlr_scene_node_reparent(&workspace->scene_tree->node, root->layers.workspaces);
        wlr_scene_node_raise_to_top(&workspace->scene_tree->node);

        struct hwd_workspace *prev_workspace = workspace;

        // Move subsequent workspaces immediately below it.
        while (workspace_index > 0) {
            workspace_index--;

            workspace = workspaces->items[workspace_index];
            wlr_scene_node_reparent(&workspace->scene_tree->node, root->layers.workspaces);
            wlr_scene_node_place_below(
                &workspace->scene_tree->node, &prev_workspace->scene_tree->node
            );

            prev_workspace = workspace;
        }

        link = &prev_workspace->scene_tree->node.link;
    }

    // Iterate over any nodes that haven't been moved to the top as a result
    // of belonging to a child and unparent them.
    link = link->prev;
    while (link != &root->layers.workspaces->children) {
        struct wlr_scene_node *node = wl_container_of(link, node, link);
        link = link->prev;
        if (node->parent == root->layers.workspaces) {
            wlr_scene_node_reparent(node, NULL);
        }
    }
}

static void
root_update_scene(struct hwd_root *root) {
    root_update_layer_workspaces(root);
}

static void
root_destroy_scene(struct hwd_root *root) {
    wlr_scene_node_destroy(&root->root_scene->tree.node);
}

static void
root_handle_transaction_before_commit(struct wl_listener *listener, void *data) {
    struct hwd_root *root = wl_container_of(listener, root, transaction_before_commit);

    hwd_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);

#ifndef NDEBUG
    assert(root->focused_surface == root_get_focused_surface(root));

    root_validate(root);
#endif
}

static void
root_copy_state(struct hwd_root_state *tgt, struct hwd_root_state *src) {
    list_t *tgt_workspaces = tgt->workspaces;

    memcpy(tgt, src, sizeof(struct hwd_root_state));

    tgt->workspaces = tgt_workspaces;
    list_clear(tgt->workspaces);
    list_cat(tgt->workspaces, src->workspaces);
}

static void
root_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hwd_root *root = wl_container_of(listener, root, transaction_commit);

    wl_list_remove(&listener->link);
    root->dirty = false;

    wl_signal_add(&root->transaction_manager->events.apply, &root->transaction_apply);

    root_copy_state(&root->committed, &root->pending);
}

static void
root_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_root *root = wl_container_of(listener, root, transaction_apply);

    wl_list_remove(&listener->link);

    root_update_scene(root);

    if (root->current.theme != root->committed.theme) {
        root->orphaned_theme = root->committed.theme;
    }

    root_copy_state(&root->current, &root->committed);
}

static void
root_handle_transaction_after_apply(struct wl_listener *listener, void *data) {
    struct hwd_root *root = wl_container_of(listener, root, transaction_after_apply);

    if (root->orphaned_theme != NULL) {
        hwd_theme_destroy(root->orphaned_theme);
        root->orphaned_theme = NULL;
    }

    wl_signal_emit_mutable(&root->events.scene_changed, root);
}

static void
root_handle_output_layout_change(struct wl_listener *listener, void *data) {
    struct hwd_root *root = wl_container_of(listener, root, output_layout_change);

    root_arrange(root);
}

struct hwd_root *
root_create(struct wl_display *display) {
    struct hwd_root *root = calloc(1, sizeof(struct hwd_root));
    if (!root) {
        wlr_log(WLR_ERROR, "Unable to allocate hwd_root");
        return NULL;
    }

    root->transaction_manager = hwd_transaction_manager_create();
    root->workspace_manager = hwd_workspace_manager_v1_create(display);

    root->transaction_before_commit.notify = root_handle_transaction_before_commit;
    root->transaction_commit.notify = root_handle_transaction_commit;
    root->transaction_apply.notify = root_handle_transaction_apply;
    root->transaction_after_apply.notify = root_handle_transaction_after_apply;

    root->output_layout = wlr_output_layout_create(display);

    wl_list_init(&root->all_outputs);
#if HAVE_XWAYLAND
    wl_list_init(&root->xwayland_unmanaged);
#endif
    wl_list_init(&root->drag_icons);
    root->outputs = create_list();
    root->pending.workspaces = create_list();
    root->committed.workspaces = create_list();
    root->current.workspaces = create_list();

    root_init_scene(root);
    root->scene_output_layout =
        wlr_scene_attach_output_layout(root->root_scene, root->output_layout);

    root->output_layout_change.notify = root_handle_output_layout_change;
    wl_signal_add(&root->output_layout->events.change, &root->output_layout_change);
    wl_signal_add(
        &root->transaction_manager->events.before_commit, &root->transaction_before_commit
    );
    wl_signal_add(&root->transaction_manager->events.after_apply, &root->transaction_after_apply);

    wl_signal_init(&root->events.focus_changed);
    wl_signal_init(&root->events.scene_changed);

    return root;
}

void
root_destroy(struct hwd_root *root) {
    root_destroy_scene(root);

    wl_list_remove(&root->output_layout_change.link);
    wl_list_remove(&root->transaction_before_commit.link);
    list_free(root->outputs);
    list_free(root->pending.workspaces);
    list_free(root->committed.workspaces);
    list_free(root->current.workspaces);
    wlr_output_layout_destroy(root->output_layout);
    hwd_transaction_manager_destroy(root->transaction_manager);

    hwd_theme_destroy(root->pending.theme);
    if (root->committed.theme != root->pending.theme) {
        hwd_theme_destroy(root->committed.theme);
    }
    if (root->current.theme != root->committed.theme) {
        hwd_theme_destroy(root->current.theme);
    }

    free(root);
}

static void
root_set_dirty(struct hwd_root *root) {
    assert(root != NULL);

    if (root->dirty) {
        return;
    }
    root->dirty = true;
    wl_signal_add(&root->transaction_manager->events.commit, &root->transaction_commit);
    hwd_transaction_manager_ensure_queued(root->transaction_manager);

    for (int i = 0; i < root->committed.workspaces->length; i++) {
        struct hwd_workspace *workspace = root->committed.workspaces->items[i];
        workspace_set_dirty(workspace);
    }

    for (int i = 0; i < root->pending.workspaces->length; i++) {
        struct hwd_workspace *workspace = root->pending.workspaces->items[i];
        workspace_set_dirty(workspace);
    }
}

void
root_arrange(struct hwd_root *root) {
    HWD_PROFILER_TRACE();

    if (config->reloading) {
        return;
    }

    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];
        output_arrange(output);
    }

    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hwd_workspace *workspace = root->pending.workspaces->items[i];
        workspace_arrange(workspace);
    }
}

static int
sort_workspace_cmp_qsort(const void *_a, const void *_b) {
    struct hwd_workspace *a = *(void **)_a;
    struct hwd_workspace *b = *(void **)_b;

    if (isdigit(a->name[0]) && isdigit(b->name[0])) {
        int a_num = strtol(a->name, NULL, 10);
        int b_num = strtol(b->name, NULL, 10);
        return (a_num < b_num) ? -1 : (a_num > b_num);
    } else if (isdigit(a->name[0])) {
        return -1;
    } else if (isdigit(b->name[0])) {
        return 1;
    }
    return 0;
}

void
root_add_workspace(struct hwd_root *root, struct hwd_workspace *workspace) {
    list_add(root->pending.workspaces, workspace);
    list_stable_sort(root->pending.workspaces, sort_workspace_cmp_qsort);

    if (root->pending.active_workspace == NULL) {
        root_set_active_workspace(root, workspace);
    }
    workspace_reconcile(workspace, root);

    root_set_dirty(root);
    workspace_set_dirty(workspace);
}

void
root_remove_workspace(struct hwd_root *root, struct hwd_workspace *workspace) {
    assert(workspace != NULL);

    int index = list_find(root->pending.workspaces, workspace);
    if (index != -1) {
        list_del(root->pending.workspaces, index);
    }

    if (root->pending.active_workspace == workspace) {
        assert(index != -1);
        int next_index = index != 0 ? index - 1 : index;

        struct hwd_workspace *next_focus = NULL;
        if (next_index < root->pending.workspaces->length) {
            next_focus = root->pending.workspaces->items[next_index];
        }

        root_set_active_workspace(root, next_focus);
    }

    workspace_reconcile_detached(workspace);

    workspace_set_dirty(workspace);
    root_set_dirty(root);
}

void
root_set_active_workspace(struct hwd_root *root, struct hwd_workspace *workspace) {
    assert(workspace != NULL);

    struct hwd_workspace *old_workspace = root->pending.active_workspace;

    if (workspace == old_workspace) {
        return;
    }

    root->pending.active_workspace = workspace;

    if (old_workspace != NULL) {
        workspace_reconcile(old_workspace, root);
        workspace_consider_destroy(old_workspace);
        workspace_set_dirty(old_workspace);
    }
    workspace_reconcile(workspace, root);
    workspace_set_dirty(workspace);

    struct hwd_output *active_output = workspace_get_active_output(workspace);
    if (active_output != NULL) {
        root->pending.active_output = active_output;
    }

    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];
        output_reconcile(output);
    }

    root_set_dirty(root);
}

struct hwd_workspace *
root_get_active_workspace(struct hwd_root *root) {
    return root->pending.active_workspace;
}

void
root_set_active_output(struct hwd_root *root, struct hwd_output *output) {
    assert(output != NULL);
    root->pending.active_output = output;
}

struct hwd_output *
root_get_active_output(struct hwd_root *root) {
    return root->pending.active_output;
}

static struct wlr_surface *
root_get_active_unmanaged(struct hwd_root *root) {
    return root->pending.active_unmanaged;
}

static struct wlr_layer_surface_v1 *
root_get_active_layer(struct hwd_root *root) {
    return root->pending.active_layer;
}

static struct hwd_window *
root_get_active_window(struct hwd_root *root) {
    struct hwd_workspace *workspace = root_get_active_workspace(root);
    assert(workspace != NULL);
    return workspace_get_active_window(workspace);
}

struct wlr_surface *
root_get_focused_unmanaged(struct hwd_root *root) {
    assert(root != NULL);

    return root->pending.active_unmanaged;
}

struct wlr_layer_surface_v1 *
root_get_focused_layer(struct hwd_root *root) {
    assert(root != NULL);

    if (root_get_active_unmanaged(root) != NULL) {
        return NULL;
    }

    return root->pending.active_layer;
}

struct hwd_window *
root_get_focused_window(struct hwd_root *root) {
    assert(root != NULL);

    if (root_get_active_unmanaged(root) != NULL) {
        return NULL;
    }

    if (root_get_active_layer(root) != NULL) {
        return NULL;
    }

    struct hwd_workspace *workspace = root_get_active_workspace(root);
    assert(workspace != NULL);
    return workspace_get_active_window(workspace);
}

struct wlr_surface *
root_get_focused_surface(struct hwd_root *root) {
    struct wlr_surface *unmanaged = root_get_focused_unmanaged(root);
    if (unmanaged != NULL) {
        return unmanaged;
    }

    struct wlr_layer_surface_v1 *layer = root_get_focused_layer(root);
    if (layer != NULL) {
        return layer->surface;
    }

    struct hwd_window *window = root_get_focused_window(root);
    if (window != NULL) {
        return window->view->surface;
    }

    return NULL;
}

void
root_set_focused_layer(struct hwd_root *root, struct wlr_layer_surface_v1 *layer) {
    if (layer != NULL && root->pending.active_unmanaged) {
        root->pending.active_unmanaged = NULL;
    }

    root->pending.active_layer = layer;
}

void
root_set_focused_window(struct hwd_root *root, struct hwd_window *window) {
    assert(window != NULL);

    struct hwd_workspace *workspace = window->pending.workspace;
    assert(workspace != NULL);

    root_set_focused_layer(root, NULL);
    root_set_focused_surface(root, NULL);

    root_set_active_workspace(root, workspace);
    workspace_set_active_window(workspace, window);
}

void
root_set_focused_surface(struct hwd_root *root, struct wlr_surface *surface) {
    if (surface != NULL) {
        root_set_focused_layer(root, NULL);

        if (root->pending.active_workspace != NULL) {
            workspace_set_active_window(root->pending.active_workspace, NULL);
        }
    }

    root->pending.active_unmanaged = surface;
}

static int
root_handle_urgent_timeout(void *data) {
    struct hwd_view *view = data;
    view_set_urgent(view, false);
    return 0;
}

void
root_commit_focus(struct hwd_root *root) {
    struct wlr_surface *active_unmanaged = root_get_active_unmanaged(root);
    struct wlr_layer_surface_v1 *active_layer = root_get_active_layer(root);
    struct hwd_window *active_window = root_get_active_window(root);

    struct wlr_surface *new_surface = NULL;
    struct wlr_layer_surface_v1 *new_layer = NULL;
    struct hwd_window *new_window = NULL;
    if (active_unmanaged != NULL) {
        new_surface = active_unmanaged;
        new_layer = NULL;
        new_window = NULL;
    } else if (active_layer != NULL) {
        new_surface = active_layer->surface;
        new_layer = active_layer;
        new_window = NULL;
    } else if (active_window != NULL) {
        new_surface = active_window->view->surface;
        new_layer = NULL;
        new_window = active_window;
    }

    struct wlr_surface *old_surface = root->focused_surface;
    struct hwd_window *old_window = root->focused_window;

    if (old_window != NULL && window_is_alive(old_window) && old_window != new_window) {
        view_close_popups(old_window->view);
        view_set_activated(old_window->view, false);

        window_set_dirty(old_window);
        if (window_is_tiling(old_window)) {
            column_set_dirty(old_window->pending.parent);
        }
    }

    if (new_window != NULL && new_window != old_window) {
        struct hwd_workspace *new_workspace = new_window->pending.workspace;
        struct hwd_view *new_view = new_window->view;

        struct hwd_workspace *old_workspace = NULL;
        if (old_window != NULL) {
            old_workspace = old_window->pending.workspace;
        }

        view_set_activated(new_view, true);

        // If window was marked as urgent, i.e. requiring attention,
        // then we usually want to clear the mark when it is focused.
        // If a user focused an urgent window accidentally, for example
        // by switching workspace, then we want to delay clearing the
        // mark a little bit to let them know that the window was
        // urgent.
        if (view_is_urgent(new_view) && !new_view->urgent_timer) {
            if (old_workspace && old_workspace != new_workspace && config->urgent_timeout > 0) {
                new_view->urgent_timer = wl_event_loop_add_timer(
                    server.wl_event_loop, root_handle_urgent_timeout, new_view
                );
                if (new_view->urgent_timer) {
                    wl_event_source_timer_update(new_view->urgent_timer, config->urgent_timeout);
                } else {
                    wlr_log_errno(WLR_ERROR, "Unable to create urgency timer");
                    root_handle_urgent_timeout(new_view);
                }
            } else {
                view_set_urgent(new_view, false);
            }
        }

        window_set_dirty(new_window);
    }

    root->focused_surface = new_surface;
    root->focused_layer = new_layer;
    root->focused_window = new_window;

    if (old_surface != new_surface) {
        struct hwd_root_focus_changed_event event = {
            .root = root, .old_focus = old_surface, .new_focus = new_surface};
        wl_signal_emit_mutable(&root->events.focus_changed, &event);
    }
}

struct hwd_workspace *
root_find_workspace(
    struct hwd_root *root, bool (*test)(struct hwd_workspace *workspace, void *data), void *data
) {
    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hwd_workspace *workspace = root->pending.workspaces->items[i];
        if (test(workspace, data)) {
            return workspace;
        }
    }
    return NULL;
}

static void
root_validate(struct hwd_root *root) {
    assert(root != NULL);

    // Validate that there is at least one workspace.
    struct hwd_workspace *active_workspace = root->pending.active_workspace;
    assert(active_workspace != NULL);
    assert(list_find(root->pending.workspaces, active_workspace) != -1);

    // Validate that the correct output is focused if workspace is in tiling
    // mode.
    if (active_workspace->pending.focus_mode == F_TILING) {
        if (active_workspace->pending.active_column) {
            assert(
                root->pending.active_output ==
                active_workspace->pending.active_column->pending.output
            );
        }
    }

    // Recursively validate each workspace.
    for (int i = 0; i < root->pending.workspaces->length; i++) {
        struct hwd_workspace *workspace = root->pending.workspaces->items[i];
        assert(workspace != NULL);

        // Validate floating windows.
        for (int j = 0; j < workspace->pending.floating->length; j++) {
            struct hwd_window *window = workspace->pending.floating->items[j];
            assert(window != NULL);

            assert(window->pending.workspace == workspace);
            assert(list_find(root->outputs, window->pending.output) != -1);
            assert(window->pending.parent == NULL);
        }

        for (int j = 0; j < workspace->pending.columns->length; j++) {
            struct hwd_column *column = workspace->pending.columns->items[j];

            assert(column->pending.workspace == workspace);
            assert(list_find(root->outputs, column->pending.output) != -1);

            for (int k = 0; k < column->pending.children->length; k++) {
                struct hwd_window *window = column->pending.children->items[k];

                assert(window->pending.parent == column);
                assert(window->pending.workspace == workspace);
                assert(window->pending.output == column->pending.output);
            }
        }
    }
}

struct hwd_output *
root_find_closest_output(struct hwd_root *root, double target_x, double target_y) {

    struct hwd_output *closest_output = NULL;
    double closest_distance = DBL_MAX;
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];
        struct wlr_box output_box;
        double closest_x, closest_y;
        output_get_box(output, &output_box);
        wlr_box_closest_point(&output_box, target_x, target_y, &closest_x, &closest_y);
        if (target_x == closest_x && target_y == closest_y) {
            closest_output = output;
            break;
        }
        double x_dist = closest_x - target_x;
        double y_dist = closest_y - target_y;
        double distance = x_dist * x_dist + y_dist * y_dist;
        if (distance < closest_distance) {
            closest_output = output;
            closest_distance = distance;
        }
    }
    return closest_output;
}

struct hwd_output *
root_get_output_at(struct hwd_root *root, double x, double y) {
    for (int i = 0; i < root->outputs->length; i++) {
        struct hwd_output *output = root->outputs->items[i];

        if (output->pending.dead) {
            continue;
        }

        struct wlr_box output_box;
        output_get_box(output, &output_box);
        if (wlr_box_contains_point(&output_box, x, y)) {
            return output;
        }
    }
    return NULL;
}

struct hwd_output *
root_get_output_in_direction(
    struct hwd_root *root, struct hwd_output *reference, enum wlr_direction direction
) {
    // TODO will need to be rewritten based on `hwd_output` to support
    // duplicated displays.
    assert(direction);
    struct wlr_box output_box;
    wlr_output_layout_get_box(root->output_layout, reference->wlr_output, &output_box);
    int lx = output_box.x + output_box.width / 2;
    int ly = output_box.y + output_box.height / 2;
    struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
        root->output_layout, direction, reference->wlr_output, lx, ly
    );
    if (!wlr_adjacent) {
        return NULL;
    }
    return output_from_wlr_output(wlr_adjacent);
}

void
root_set_theme(struct hwd_root *root, struct hwd_theme *theme) {
    assert(root != NULL);
    assert(theme != root->pending.theme);

    if (root->pending.theme != root->current.theme) {
        hwd_theme_destroy(root->pending.theme);
    }

    root->pending.theme = theme;

    root_arrange(root);
    root_set_dirty(root);
}

struct hwd_theme *
root_get_theme(struct hwd_root *root) {
    return root->pending.theme;
}

struct hwd_transaction_manager *
root_get_transaction_manager(struct hwd_root *root) {
    assert(root != NULL);

    return root->transaction_manager;
}
