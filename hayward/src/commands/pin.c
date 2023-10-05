#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/window.h>

static const char expected_syntax[] = "Expected `pin [enable|disable|toggle]`";

struct cmd_results *
cmd_pin(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "pin", EXPECTED_AT_MOST, 2))) {
        return error;
    }
    struct hwd_window *window = config->handler_context.window;

    if (!window) {
        // If the focus is not a window, do nothing successfully
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    bool is_pinned = window->pending.pinned;

    if (argc != 1) {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    bool enable;
    if (strcasecmp(argv[0], "toggle") == 0) {
        enable = !is_pinned;
    } else if (strcasecmp(argv[0], "enable") == 0) {
        enable = true;
    } else if (strcasecmp(argv[0], "disable") == 0) {
        enable = false;
    } else {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    if (enable != is_pinned) {
        window->pending.pinned = enable;
        if (window->pending.parent != NULL) {
            arrange_column(window->pending.parent);
        }
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
