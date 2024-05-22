#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <strings.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

static const char expected_syntax[] = "Expected 'layout stacking|split'";

struct cmd_results *
cmd_layout(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "layout", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID, "Can't run this command while there's no outputs connected."
        );
    }
    struct hwd_window *window = config->handler_context.window;
    struct hwd_workspace *workspace = config->handler_context.workspace;

    if (!window) {
        return cmd_results_new(CMD_INVALID, "No window selected");
    }

    if (window_is_floating(window)) {
        return cmd_results_new(CMD_FAILURE, "Unable to change the layout of floating windows");
    }

    struct hwd_column *column = window->column;
    if (!column) {
        return cmd_results_new(CMD_FAILURE, "Window is not a member of a column");
    }

    enum hwd_column_layout old_layout = column->layout;

    enum hwd_column_layout new_layout;
    if (strcasecmp(argv[0], "split") == 0) {
        new_layout = L_SPLIT;
    } else if (strcasecmp(argv[0], "stacking") == 0) {
        new_layout = L_STACKED;
    } else {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    if (new_layout != old_layout) {
        column->layout = new_layout;
        workspace_set_dirty(workspace);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
