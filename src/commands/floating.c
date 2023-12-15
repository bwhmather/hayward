#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/tree.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>
#include <hayward/util.h>

struct cmd_results *
cmd_floating(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "floating", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID, "Can't run this command while there's no outputs connected."
        );
    }
    struct hwd_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_INVALID, "Can only float windows");
    }

    struct hwd_workspace *workspace = window->pending.workspace;
    struct hwd_output *output = window_get_output(window);
    struct hwd_window *focused_window = workspace_get_active_window(workspace);

    window_end_mouse_operation(window);

    if (parse_boolean(argv[0], window_is_floating(window))) {
        window_detach(window);

        struct hwd_column *column = NULL;
        for (int i = 0; i < workspace->pending.columns->length; i++) {
            struct hwd_column *candidate_column = workspace->pending.columns->items[i];
            if (candidate_column->pending.output == output) {
                column = candidate_column;
                break;
            }
        }
        if (workspace->pending.active_column != NULL &&
            workspace->pending.active_column->pending.output == output) {
            column = workspace->pending.active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_column_first(workspace, output, column);
        }

        hwd_move_window_to_column(window, column);

    } else {
        struct hwd_column *old_parent = window->pending.parent;
        window_detach(window);
        workspace_add_floating(workspace, window);
        window_floating_set_default_size(window);
        window_floating_resize_and_center(window);
        if (old_parent) {
            column_consider_destroy(old_parent);
        }
    }

    if (window == focused_window) {
        workspace_set_active_window(workspace, window);
    }

    workspace_arrange(workspace);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
