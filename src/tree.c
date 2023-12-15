#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/tree.h"

#include <stdbool.h>
#include <wlr/types/wlr_output_layout.h>

#include <hayward/list.h>
#include <hayward/log.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

static void
hwd_move_window_to_column_from_maybe_direction(
    struct hwd_window *window, struct hwd_column *column, bool has_move_dir,
    enum wlr_direction move_dir
) {
    if (window->pending.parent == column) {
        return;
    }

    struct hwd_workspace *old_workspace = window->pending.workspace;

    if (has_move_dir && (move_dir == WLR_DIRECTION_UP || move_dir == WLR_DIRECTION_DOWN)) {
        hwd_log(HWD_DEBUG, "Reparenting window (parallel)");
        int index = move_dir == WLR_DIRECTION_DOWN ? 0 : column->pending.children->length;
        window_detach(window);
        column_insert_child(column, window, index);
        window->pending.width = window->pending.height = 0;
    } else {
        hwd_log(HWD_DEBUG, "Reparenting window (perpendicular)");
        struct hwd_window *target_sibling = column->pending.active_child;
        window_detach(window);
        if (target_sibling) {
            column_add_sibling(target_sibling, window, 1);
        } else {
            column_add_child(column, window);
        }
    }

    if (column->pending.workspace) {
        workspace_detect_urgent(column->pending.workspace);
    }

    if (old_workspace && old_workspace != column->pending.workspace) {
        workspace_detect_urgent(old_workspace);
    }
}

void
hwd_move_window_to_column_from_direction(
    struct hwd_window *window, struct hwd_column *column, enum wlr_direction move_dir
) {
    hwd_move_window_to_column_from_maybe_direction(window, column, true, move_dir);
}

void
hwd_move_window_to_column(struct hwd_window *window, struct hwd_column *column) {
    hwd_move_window_to_column_from_maybe_direction(window, column, false, WLR_DIRECTION_DOWN);
}

void
hwd_move_window_to_workspace(struct hwd_window *window, struct hwd_workspace *workspace) {
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(workspace != NULL, "Expected workspace");

    if (workspace == window->pending.workspace) {
        return;
    }

    if (window_is_floating(window)) {
        window_detach(window);
        workspace_add_floating(workspace, window);
        window_handle_fullscreen_reparent(window);
    } else {
        struct hwd_output *output = window->pending.parent->pending.output;
        struct hwd_column *column = NULL;

        for (int i = 0; i < workspace->pending.columns->length; i++) {
            struct hwd_column *candidate_column = workspace->pending.columns->items[i];

            if (candidate_column->pending.output != output) {
                continue;
            }

            if (column != NULL) {
                continue;
            }

            column = candidate_column;
        }
        if (workspace->pending.active_column != NULL &&
            workspace->pending.active_column->pending.output == output) {
            column = workspace->pending.active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_column_first(workspace, output, column);
        }

        window->pending.width = window->pending.height = 0;

        hwd_move_window_to_column(window, column);
    }
}

void
hwd_move_window_to_output_from_direction(
    struct hwd_window *window, struct hwd_output *output, enum wlr_direction move_dir
) {
    hwd_assert(window != NULL, "Expected window");
    hwd_assert(output != NULL, "Expected output");

    struct hwd_workspace *workspace = window->pending.workspace;
    hwd_assert(workspace != NULL, "Window is not attached to a workspace");

    // TODO this should be derived from the window's current position.
    struct hwd_output *old_output = workspace_get_active_output(workspace);
    if (window_is_floating(window)) {
        if (old_output != output && !window->pending.fullscreen) {
            window_floating_move_to_center(window);
        }

        return;
    } else {
        struct hwd_column *column = NULL;

        for (int i = 0; i < workspace->pending.columns->length; i++) {
            struct hwd_column *candidate_column = workspace->pending.columns->items[i];

            if (candidate_column->pending.output != output) {
                continue;
            }

            if (move_dir == WLR_DIRECTION_LEFT || column == NULL) {
                column = candidate_column;
            }
        }
        if (workspace->pending.active_column->pending.output == output &&
            move_dir != WLR_DIRECTION_UP && move_dir == WLR_DIRECTION_DOWN) {
            column = workspace->pending.active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_column_first(workspace, output, column);
        }

        window->pending.width = window->pending.height = 0;

        hwd_move_window_to_column_from_direction(window, column, move_dir);
    }
}

void
hwd_move_window_to_output(struct hwd_window *window, struct hwd_output *output) {
    hwd_move_window_to_output_from_direction(window, output, 0);
}