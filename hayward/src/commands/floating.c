#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/commands.h"

#include <hayward-common/list.h>
#include <hayward-common/util.h>

#include <hayward/config.h>
#include <hayward/tree.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>

#include <config.h>

struct cmd_results *
cmd_floating(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "floating", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID,
            "Can't run this command while there's no outputs connected."
        );
    }
    struct hayward_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_INVALID, "Can only float windows");
    }

    if (parse_boolean(argv[0], window_is_floating(window))) {
        hayward_move_window_to_floating(window);
    } else {
        hayward_move_window_to_tiling(window);
    }

    arrange_workspace(window->pending.workspace);

    return cmd_results_new(CMD_SUCCESS, NULL);
}