#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/root.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
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
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/desktop/transaction.h>
#include <hayward/ipc-server.h>
#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/column.h>
#include <hayward/tree/node.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

struct hayward_root *root;

static void
root_copy_state(
    struct hayward_root_state *tgt, struct hayward_root_state *src
) {
    list_t *tgt_workspaces = tgt->workspaces;

    memcpy(tgt, src, sizeof(struct hayward_root_state));

    tgt->workspaces = tgt_workspaces;
    list_clear(tgt->workspaces);
    list_cat(tgt->workspaces, src->workspaces);
}

static void
root_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hayward_root *root =
        wl_container_of(listener, root, transaction_commit);

    wl_list_remove(&listener->link);
    root->dirty = false;

    transaction_add_apply_listener(&root->transaction_apply);

    root_copy_state(&root->committed, &root->pending);
}

static void
root_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hayward_root *root =
        wl_container_of(listener, root, transaction_apply);

    wl_list_remove(&listener->link);

    root_copy_state(&root->current, &root->committed);

    if (root->current.dead) {
        root_destroy(root);
    }
}

static void
output_layout_handle_change(struct wl_listener *listener, void *data) {
    arrange_root();
    transaction_commit_dirty();
}

struct hayward_root *
root_create(void) {
    struct hayward_root *root = calloc(1, sizeof(struct hayward_root));
    if (!root) {
        hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_root");
        return NULL;
    }
    node_init(&root->node, N_ROOT, root);

    root->transaction_commit.notify = root_handle_transaction_commit;
    root->transaction_apply.notify = root_handle_transaction_apply;

    root->output_layout = wlr_output_layout_create();
    wl_list_init(&root->all_outputs);
#if HAVE_XWAYLAND
    wl_list_init(&root->xwayland_unmanaged);
#endif
    wl_list_init(&root->drag_icons);
    wl_signal_init(&root->events.new_node);
    root->outputs = create_list();
    root->pending.workspaces = create_list();
    root->committed.workspaces = create_list();
    root->current.workspaces = create_list();

    root->output_layout_change.notify = output_layout_handle_change;
    wl_signal_add(
        &root->output_layout->events.change, &root->output_layout_change
    );
    return root;
}

void
root_destroy(struct hayward_root *root) {
    wl_list_remove(&root->output_layout_change.link);
    list_free(root->outputs);
    list_free(root->pending.workspaces);
    list_free(root->committed.workspaces);
    list_free(root->current.workspaces);
    wlr_output_layout_destroy(root->output_layout);
    free(root);
}

void
root_set_dirty(struct hayward_root *root) {
    hayward_assert(root != NULL, "Expected root");

    if (root->dirty) {
        return;
    }

    root->dirty = true;
    transaction_add_commit_listener(&root->transaction_commit);
    transaction_ensure_queued();

    for (int i = 0; i < root->committed.workspaces->length; i++) {
        struct hayward_workspace *workspace =
            root->committed.workspaces->items[i];
        workspace_set_dirty(workspace);
    }

    for (int i = 0; i < root->pending.workspaces->length; i++) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        workspace_set_dirty(workspace);
    }
}

struct pid_workspace {
    pid_t pid;
    char *workspace;
    struct timespec time_added;

    struct hayward_output *output;
    struct wl_listener output_destroy;

    struct wl_list link;
};

static struct wl_list pid_workspaces;

/**
 * Get the pid of a parent process given the pid of a child process.
 *
 * Returns the parent pid or NULL if the parent pid cannot be determined.
 */
static pid_t
get_parent_pid(pid_t child) {
    pid_t parent = -1;
    char file_name[100];
    char *buffer = NULL;
    const char *sep = " ";
    FILE *stat = NULL;
    size_t buf_size = 0;

    snprintf(file_name, sizeof(file_name), "/proc/%d/stat", child);

    if ((stat = fopen(file_name, "r"))) {
        if (getline(&buffer, &buf_size, stat) != -1) {
            strtok(buffer, sep);             // pid
            strtok(NULL, sep);               // executable name
            strtok(NULL, sep);               // state
            char *token = strtok(NULL, sep); // parent pid
            parent = strtol(token, NULL, 10);
        }
        free(buffer);
        fclose(stat);
    }

    if (parent) {
        return (parent == child) ? -1 : parent;
    }

    return -1;
}

static void
pid_workspace_destroy(struct pid_workspace *pw) {
    wl_list_remove(&pw->output_destroy.link);
    wl_list_remove(&pw->link);
    free(pw->workspace);
    free(pw);
}

struct hayward_workspace *
root_workspace_for_pid(pid_t pid) {
    if (!pid_workspaces.prev && !pid_workspaces.next) {
        wl_list_init(&pid_workspaces);
        return NULL;
    }

    struct hayward_workspace *workspace = NULL;
    struct pid_workspace *pw = NULL;

    hayward_log(HAYWARD_DEBUG, "Looking up workspace for pid %d", pid);

    do {
        struct pid_workspace *_pw = NULL;
        wl_list_for_each(_pw, &pid_workspaces, link) {
            if (pid == _pw->pid) {
                pw = _pw;
                hayward_log(
                    HAYWARD_DEBUG,
                    "found pid_workspace for pid %d, workspace %s", pid,
                    pw->workspace
                );
                goto found;
            }
        }
        pid = get_parent_pid(pid);
    } while (pid > 1);
found:

    if (pw && pw->workspace) {
        workspace = workspace_by_name(pw->workspace);

        if (!workspace) {
            hayward_log(
                HAYWARD_DEBUG,
                "Creating workspace %s for pid %d because it disappeared",
                pw->workspace, pid
            );

            workspace = workspace_create(pw->workspace);
        }

        pid_workspace_destroy(pw);
    }

    return workspace;
}

static void
pw_handle_output_destroy(struct wl_listener *listener, void *data) {
    struct pid_workspace *pw = wl_container_of(listener, pw, output_destroy);
    pw->output = NULL;
    wl_list_remove(&pw->output_destroy.link);
    wl_list_init(&pw->output_destroy.link);
}

void
root_record_workspace_pid(pid_t pid) {
    hayward_log(HAYWARD_DEBUG, "Recording workspace for process %d", pid);
    if (!pid_workspaces.prev && !pid_workspaces.next) {
        wl_list_init(&pid_workspaces);
    }

    struct hayward_workspace *workspace = root_get_active_workspace();
    if (!workspace) {
        hayward_log(HAYWARD_DEBUG, "Bailing out, no workspace");
        return;
    }
    struct hayward_output *output = root_get_active_output();
    if (!output) {
        hayward_log(HAYWARD_DEBUG, "Bailing out, no output");
        return;
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    // Remove expired entries
    static const int timeout = 60;
    struct pid_workspace *old, *_old;
    wl_list_for_each_safe(old, _old, &pid_workspaces, link) {
        if (now.tv_sec - old->time_added.tv_sec >= timeout) {
            pid_workspace_destroy(old);
        }
    }

    struct pid_workspace *pw = calloc(1, sizeof(struct pid_workspace));
    pw->workspace = strdup(workspace->name);
    pw->output = output;
    pw->pid = pid;
    memcpy(&pw->time_added, &now, sizeof(struct timespec));
    pw->output_destroy.notify = pw_handle_output_destroy;
    wl_signal_add(&output->wlr_output->events.destroy, &pw->output_destroy);
    wl_list_insert(&pid_workspaces, &pw->link);
}

void
root_remove_workspace_pid(pid_t pid) {
    if (!pid_workspaces.prev || !pid_workspaces.next) {
        return;
    }

    struct pid_workspace *pw, *tmp;
    wl_list_for_each_safe(pw, tmp, &pid_workspaces, link) {
        if (pid == pw->pid) {
            pid_workspace_destroy(pw);
            return;
        }
    }
}

struct hayward_output *
root_find_output(
    bool (*test)(struct hayward_output *output, void *data), void *data
) {
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hayward_output *output = root->outputs->items[i];
        if (test(output, data)) {
            return output;
        }
    }
    return NULL;
}

void
root_get_box(struct hayward_root *root, struct wlr_box *box) {
    box->x = root->x;
    box->y = root->y;
    box->width = root->width;
    box->height = root->height;
}

void
root_rename_pid_workspaces(const char *old_name, const char *new_name) {
    if (!pid_workspaces.prev && !pid_workspaces.next) {
        wl_list_init(&pid_workspaces);
    }

    struct pid_workspace *pw = NULL;
    wl_list_for_each(pw, &pid_workspaces, link) {
        if (strcmp(pw->workspace, old_name) == 0) {
            free(pw->workspace);
            pw->workspace = strdup(new_name);
        }
    }
}

void
root_add_workspace(struct hayward_workspace *workspace) {
    list_add(root->pending.workspaces, workspace);
    if (root->pending.active_workspace == NULL) {
        root_set_active_workspace(workspace);
    }
    workspace_reconcile(workspace);

    root_set_dirty(root);
    workspace_set_dirty(workspace);
}

void
root_remove_workspace(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    int index = list_find(root->pending.workspaces, workspace);
    if (index != -1) {
        list_del(root->pending.workspaces, index);
    }

    if (root->pending.active_workspace == workspace) {
        hayward_assert(index != -1, "Workspace is active but not attached");
        int next_index = index != 0 ? index - 1 : index;

        struct hayward_workspace *next_focus = NULL;
        if (next_index < root->pending.workspaces->length) {
            next_focus = root->pending.workspaces->items[next_index];
        }

        root_set_active_workspace(next_focus);
    }

    workspace_reconcile_detached(workspace);

    workspace_set_dirty(workspace);
    root_set_dirty(root);
}

static int
sort_workspace_cmp_qsort(const void *_a, const void *_b) {
    struct hayward_workspace *a = *(void **)_a;
    struct hayward_workspace *b = *(void **)_b;

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
root_sort_workspaces(void) {
    list_stable_sort(root->pending.workspaces, sort_workspace_cmp_qsort);
}

void
root_set_active_workspace(struct hayward_workspace *workspace) {
    hayward_assert(workspace != NULL, "Expected workspace");

    struct hayward_workspace *old_workspace = root->pending.active_workspace;

    if (workspace == old_workspace) {
        return;
    }

    root->pending.active_workspace = workspace;

    if (old_workspace != NULL) {
        workspace_reconcile(old_workspace);
        workspace_set_dirty(old_workspace);
    }
    workspace_reconcile(workspace);
    workspace_set_dirty(workspace);

    struct hayward_output *active_output =
        workspace_get_active_output(workspace);
    if (active_output != NULL) {
        root->pending.active_output = active_output;
    }

    root_set_dirty(root);
}

struct hayward_workspace *
root_get_active_workspace(void) {
    return root->pending.active_workspace;
}

struct hayward_workspace *
root_get_current_active_workspace(void) {
    return root->current.active_workspace;
}

void
root_set_active_output(struct hayward_output *output) {
    hayward_assert(output != NULL, "Expected output");
    root->pending.active_output = output;
}

struct hayward_output *
root_get_active_output(void) {
    return root->pending.active_output;
}

struct hayward_output *
root_get_current_active_output(void) {
    return root->current.active_output;
}

void
root_set_focused_window(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");

    struct hayward_workspace *workspace = window->pending.workspace;
    hayward_assert(workspace != NULL, "Expected workspace");

    root_set_focused_layer(NULL);
    root_set_focused_surface(NULL);

    root_set_active_workspace(workspace);
    workspace_set_active_window(workspace, window);
}

struct hayward_window *
root_get_active_window(void) {
    struct hayward_workspace *workspace = root_get_active_workspace();
    hayward_assert(workspace != NULL, "Expected workspace");

    return workspace_get_active_window(workspace);
}

struct hayward_window *
root_get_focused_window(void) {
    if (root->pending.focused_layer != NULL) {
        return NULL;
    }
    return root_get_active_window();
}

void
root_set_focused_layer(struct wlr_layer_surface_v1 *layer) {
    root->pending.focused_layer = layer;
}

void
root_set_focused_surface(struct wlr_surface *surface) {
    if (surface != NULL) {
        root_set_focused_layer(NULL);

        if (root->pending.active_workspace != NULL) {
            workspace_set_active_window(root->pending.active_workspace, NULL);
        }
    }

    root->pending.focused_surface = surface;
}

struct wlr_layer_surface_v1 *
root_get_focused_layer(void) {
    return root->pending.focused_layer;
}

struct wlr_surface *
root_get_focused_surface(void) {
    if (root->pending.focused_layer != NULL) {
        return root->pending.focused_layer->surface;
    }

    struct hayward_window *window = root_get_focused_window();
    if (window != NULL) {
        return window->view->surface;
    }

    if (root->pending.focused_surface != NULL) {
        return root->pending.focused_surface;
    }

    return NULL;
}

static int
handle_urgent_timeout(void *data) {
    struct hayward_view *view = data;
    view_set_urgent(view, false);
    return 0;
}

void
root_commit_focus(void) {
    struct hayward_window *old_window = root->focused_window;
    struct hayward_window *new_window = root_get_focused_window();

    struct hayward_workspace *old_workspace = root->focused_workspace;
    struct hayward_workspace *new_workspace = root_get_active_workspace();

    if (old_window == new_window && old_workspace == new_workspace) {
        return;
    }

    if (old_window && new_window != old_window) {
        view_close_popups(old_window->view);
        view_set_activated(old_window->view, false);

        window_set_dirty(old_window);
        if (window_is_tiling(old_window)) {
            column_set_dirty(old_window->pending.parent);
        }
    }

    if (new_window && new_window != old_window) {
        struct hayward_view *view = new_window->view;

        view_set_activated(view, true);

        // If window was marked as urgent, i.e. requiring attention,
        // then we usually want to clear the mark when it is focused.
        // If a user focused an urgent window accidentally, for example
        // by switching workspace, then we want to delay clearing the
        // mark a little bit to let them know that the window was
        // urgent.
        if (view_is_urgent(view) && !view->urgent_timer) {
            if (old_workspace && old_workspace != new_workspace &&
                config->urgent_timeout > 0) {
                view->urgent_timer = wl_event_loop_add_timer(
                    server.wl_event_loop, handle_urgent_timeout, view
                );
                if (view->urgent_timer) {
                    wl_event_source_timer_update(
                        view->urgent_timer, config->urgent_timeout
                    );
                } else {
                    hayward_log_errno(
                        HAYWARD_ERROR, "Unable to create urgency timer"
                    );
                    handle_urgent_timeout(view);
                }
            } else {
                view_set_urgent(view, false);
            }
        }

        window_set_dirty(new_window);
        if (window_is_tiling(new_window)) {
            column_set_dirty(new_window->pending.parent);
        }
    }

    root->focused_workspace = new_workspace;
    root->focused_window = new_window;

    // Emit ipc events
    if (new_window != old_window) {
        ipc_event_window(new_window, "focus");
    }
    if (new_workspace != old_workspace) {
        ipc_event_workspace(old_workspace, new_workspace, "focus");
    }
}

void
root_for_each_workspace(
    void (*f)(struct hayward_workspace *workspace, void *data), void *data
) {
    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        f(workspace, data);
    }
}

void
root_for_each_window(
    void (*f)(struct hayward_window *window, void *data), void *data
) {
    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        workspace_for_each_window(workspace, f, data);
    }
}

struct hayward_workspace *
root_find_workspace(
    bool (*test)(struct hayward_workspace *workspace, void *data), void *data
) {
    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        if (test(workspace, data)) {
            return workspace;
        }
    }
    return NULL;
}

struct hayward_window *
root_find_window(
    bool (*test)(struct hayward_window *window, void *data), void *data
) {
    struct hayward_window *result = NULL;
    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        if ((result = workspace_find_window(workspace, test, data))) {
            return result;
        }
    }
    return NULL;
}
