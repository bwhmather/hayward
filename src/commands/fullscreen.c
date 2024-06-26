#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>

static const char expected_syntax[] = "Expected `fullscreen [enable|disable|toggle]`";

struct cmd_results *
cmd_fullscreen(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "fullscreen", EXPECTED_AT_MOST, 2))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_FAILURE, "Can't run this command while there's no outputs connected."
        );
    }
    struct hwd_window *window = config->handler_context.window;

    if (!window) {
        // If the focus is not a window, do nothing successfully
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    bool is_fullscreen = window_is_fullscreen(window);

    if (argc != 1) {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    bool enable;
    if (strcasecmp(argv[0], "toggle") == 0) {
        enable = !is_fullscreen;
    } else if (strcasecmp(argv[0], "enable") == 0) {
        enable = true;
    } else if (strcasecmp(argv[0], "disable") == 0) {
        enable = false;
    } else {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    if (enable) {
        window_fullscreen(window);
    } else {
        window_unfullscreen(window);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
