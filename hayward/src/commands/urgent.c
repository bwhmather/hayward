#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <stdbool.h>
#include <string.h>

#include <hayward-common/util.h>

#include <hayward/config.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

#include <config.h>

struct cmd_results *
cmd_urgent(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "urgent", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    struct hwd_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_FAILURE, "No current window");
    }
    struct hwd_view *view = window->view;

    if (strcmp(argv[0], "allow") == 0) {
        view->allow_request_urgent = true;
    } else if (strcmp(argv[0], "deny") == 0) {
        view->allow_request_urgent = false;
    } else {
        view_set_urgent(view, parse_boolean(argv[0], view_is_urgent(view)));
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
