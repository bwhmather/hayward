#ifndef HWD_TREE_WORKSPACE_H
#define HWD_TREE_WORKSPACE_H

#include <stdbool.h>
#include <stddef.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_scene.h>

#include <hayward/desktop/hwd_workspace_management_v1.h>
#include <hayward/list.h>
#include <hayward/tree/column.h>
#include <hayward/tree/window.h>

enum hwd_focus_mode {
    F_TILING,
    F_FLOATING,
};

struct hwd_view;

struct hwd_workspace_state {
    list_t *floating; // struct hwd_window
    list_t *columns;  // struct hwd_column

    bool focused;

    bool dead;
};

struct hwd_workspace {
    size_t id;

    struct hwd_workspace_state pending;
    struct hwd_workspace_state committed;
    struct hwd_workspace_state current;

    bool dirty;
    bool dead;

    char *name;

    bool urgent;

    struct hwd_root *root;

    list_t *floating; // struct hwd_window
    list_t *columns;  // struct hwd_column

    // The column that should be given focus if this workspace is focused and
    // focus_mode is F_TILING.
    struct hwd_column *active_column;

    enum hwd_focus_mode focus_mode;

    struct hwd_workspace_handle_v1 *workspace_handle;

    struct wlr_scene_tree *scene_tree;

    struct {
        struct wlr_scene_tree *separators;
        struct wlr_scene_tree *tiling;
        struct wlr_scene_tree *floating;
    } layers;

    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal begin_destroy;
    } events;
};

struct hwd_workspace *
workspace_create(struct hwd_root *root, const char *name);

bool
workspace_is_alive(struct hwd_workspace *workspace);

void
workspace_consider_destroy(struct hwd_workspace *workspace);

void
workspace_set_dirty(struct hwd_workspace *workspace);

struct hwd_workspace *
workspace_by_name(const char *);

void
workspace_arrange(struct hwd_workspace *workspace);

void
workspace_detect_urgent(struct hwd_workspace *workspace);

bool
workspace_is_visible(struct hwd_workspace *workspace);

void
workspace_add_floating(struct hwd_workspace *workspace, struct hwd_window *window);

void
workspace_remove_floating(struct hwd_workspace *workspace, struct hwd_window *window);

struct hwd_column *
workspace_get_column_first(struct hwd_workspace *workspace, struct hwd_output *output);

struct hwd_column *
workspace_get_column_last(struct hwd_workspace *workspace, struct hwd_output *output);

struct hwd_column *
workspace_get_column_before(struct hwd_workspace *workspace, struct hwd_column *column);

struct hwd_column *
workspace_get_column_after(struct hwd_workspace *workspace, struct hwd_column *column);

void
workspace_insert_column_first(
    struct hwd_workspace *workspace, struct hwd_output *output, struct hwd_column *column
);
void
workspace_insert_column_last(
    struct hwd_workspace *workspace, struct hwd_output *output, struct hwd_column *column
);

void
workspace_insert_column_before(
    struct hwd_workspace *workspace, struct hwd_column *fixed, struct hwd_column *column
);
void
workspace_insert_column_after(
    struct hwd_workspace *workspace, struct hwd_column *fixed, struct hwd_column *column
);

struct hwd_column *
workspace_get_column_at(struct hwd_workspace *workspace, double x, double y);

struct hwd_output *
workspace_get_active_output(struct hwd_workspace *workspace);

struct hwd_window *
workspace_get_active_tiling_window(struct hwd_workspace *workspace);
struct hwd_window *
workspace_get_active_floating_window(struct hwd_workspace *workspace);
struct hwd_window *
workspace_get_active_window(struct hwd_workspace *workspace);

void
workspace_set_active_window(struct hwd_workspace *workspace, struct hwd_window *window);

struct hwd_window *
workspace_get_floating_window_at(struct hwd_workspace *workspace, double x, double y);

/**
 * Traverses all windows on the workspace to find the first fullscreen window on
 * the requested output.
 *
 * If the workspace is active, the result of this function will be cached on
 * output->pending.fullscreen_window.
 */
struct hwd_window *
workspace_get_fullscreen_window_for_output(
    struct hwd_workspace *workspace, struct hwd_output *output
);

#endif
