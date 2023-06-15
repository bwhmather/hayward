#ifndef HAYWARD_TREE_WORKSPACE_H
#define HAYWARD_TREE_WORKSPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>

#include <hayward-common/list.h>

#include <hayward/config.h>
#include <hayward/tree/column.h>
#include <hayward/tree/window.h>

enum hayward_focus_mode {
    F_TILING,
    F_FLOATING,
};

struct hayward_view;

struct hayward_workspace_state {
    list_t *floating; // struct hayward_window
    list_t *tiling;   // struct hayward_column

    // Cached backlink to root of tree.
    struct hayward_root *root;

    // The column that should be given focus if this workspace is focused and
    // focus_mode is F_TILING.
    struct hayward_column *active_column;

    enum hayward_focus_mode focus_mode;
    bool focused;

    bool dead;
};

struct hayward_workspace {
    size_t id;

    struct hayward_workspace_state pending;
    struct hayward_workspace_state committed;
    struct hayward_workspace_state current;

    bool dirty;

    char *name;

    bool urgent;

    struct wlr_scene_tree *scene_tree;

    struct {
        struct wlr_scene_tree *tiling;
        struct wlr_scene_tree *floating;
        struct wlr_scene_tree *fullscreen;
    } layers;

    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal begin_destroy;
    } events;
};

struct hayward_workspace *
workspace_create(const char *name);

bool
workspace_is_alive(struct hayward_workspace *workspace);

void
workspace_begin_destroy(struct hayward_workspace *workspace);

void
workspace_consider_destroy(struct hayward_workspace *workspace);

void
workspace_set_dirty(struct hayward_workspace *workspace);

struct hayward_workspace *
workspace_by_name(const char *);

bool
workspace_is_visible(struct hayward_workspace *workspace);

void
workspace_detect_urgent(struct hayward_workspace *workspace);

void
workspace_detach(struct hayward_workspace *workspace);

void
workspace_reconcile(
    struct hayward_workspace *workspace, struct hayward_root *root
);
void
workspace_reconcile_detached(struct hayward_workspace *workspace);

void
workspace_add_floating(
    struct hayward_workspace *workspace, struct hayward_window *container
);

void
workspace_remove_floating(
    struct hayward_workspace *workspace, struct hayward_window *window
);

void
workspace_insert_tiling(
    struct hayward_workspace *workspace, struct hayward_output *output,
    struct hayward_column *column, int index
);
void
workspace_remove_tiling(
    struct hayward_workspace *workspace, struct hayward_column *column
);

size_t
workspace_num_tiling_views(struct hayward_workspace *workspace);

struct hayward_output *
workspace_get_active_output(struct hayward_workspace *workspace);

struct hayward_window *
workspace_get_active_tiling_window(struct hayward_workspace *workspace);
struct hayward_window *
workspace_get_active_floating_window(struct hayward_workspace *workspace);
struct hayward_window *
workspace_get_active_window(struct hayward_workspace *workspace);
struct hayward_window *
workspace_get_committed_active_window(struct hayward_workspace *workspace);

void
workspace_set_active_window(
    struct hayward_workspace *workspace, struct hayward_window *window
);

/**
 * Traverses all windows on the workspace to find the first fullscreen window on
 * the requested output.
 *
 * If the workspace is active, the result of this function will be cached on
 * output->pending.fullscreen_window.
 */
struct hayward_window *
workspace_get_fullscreen_window_for_output(
    struct hayward_workspace *workspace, struct hayward_output *output
);

#endif
