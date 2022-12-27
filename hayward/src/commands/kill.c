#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/commands.h"

#include <hayward-common/list.h>

#include <hayward/config.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

static void
close_window_iterator(struct hayward_window *window, void *data) {
    view_close(window->view);
}

struct cmd_results *
cmd_kill(int argc, char **argv) {
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID,
            "Can't run this command while there's no outputs connected."
        );
    }
    struct hayward_window *window = config->handler_context.window;
    struct hayward_workspace *workspace = config->handler_context.workspace;

    if (window) {
        close_window_iterator(window, NULL);
    } else {
        workspace_for_each_window(workspace, close_window_iterator, NULL);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
