#include <stdbool.h>
#include <string.h>
#include <strings.h>

#include "hayward-common/log.h"

#include "hayward/commands.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/window.h"
#include "hayward/tree/workspace.h"

static const char expected_syntax[] = "Expected 'layout stacking|split'";

struct cmd_results *
cmd_layout(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "layout", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID,
            "Can't run this command while there's no outputs connected."
        );
    }
    struct hayward_window *window = config->handler_context.window;
    struct hayward_workspace *workspace = config->handler_context.workspace;

    if (!window) {
        return cmd_results_new(CMD_INVALID, "No window selected");
    }

    if (window_is_floating(window)) {
        return cmd_results_new(
            CMD_FAILURE, "Unable to change the layout of floating containers"
        );
    }

    struct hayward_column *column = window->pending.parent;
    if (!column) {
        return cmd_results_new(
            CMD_FAILURE, "Window is not a member of a column"
        );
    }

    enum hayward_column_layout old_layout = column->pending.layout;

    enum hayward_column_layout new_layout;
    if (strcasecmp(argv[0], "split") == 0) {
        new_layout = L_SPLIT;
    } else if (strcasecmp(argv[0], "stacking") == 0) {
        new_layout = L_STACKED;
    } else {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    if (new_layout != old_layout) {
        column->pending.layout = new_layout;
        arrange_workspace(workspace);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
