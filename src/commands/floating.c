#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>
#include <hayward/util.h>

struct cmd_results *
cmd_floating(int argc, char **argv) {
    HWD_PROFILER_TRACE();

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

    struct hwd_workspace *workspace = window->workspace;
    struct hwd_output *output = window_get_output(window);
    struct hwd_window *focused_window = workspace_get_active_window(workspace);

    window_end_mouse_operation(window);

    if (parse_boolean(argv[0], window_is_floating(window))) {
        struct hwd_column *old_column = window->column;
        window_detach(window);
        workspace_add_floating(workspace, window);
        if (old_column) {
            column_consider_destroy(old_column);
        }
    } else {
        window_detach(window);

        struct hwd_column *column = NULL;
        for (int i = 0; i < workspace->columns->length; i++) {
            struct hwd_column *candidate_column = workspace->columns->items[i];
            if (candidate_column->output == output) {
                column = candidate_column;
                break;
            }
        }
        if (workspace->active_column != NULL && workspace->active_column->output == output) {
            column = workspace->active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_column_first(workspace, output, column);
        }

        struct hwd_window *target_sibling = column->active_child;
        if (target_sibling) {
            column_add_sibling(target_sibling, window, 1);
        } else {
            column_add_child(column, window);
        }
    }

    if (window == focused_window) {
        workspace_set_active_window(workspace, window);
    }

    workspace_set_dirty(workspace);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
