#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/commands.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <hayward/config.h>
#include <hayward/input/cursor.h>

#include <config.h>

struct cmd_results *
input_cmd_scroll_button(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "scroll_button", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    if (strcmp(*argv, "disable") == 0) {
        ic->scroll_button = 0;
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    char *message = NULL;
    uint32_t button = get_mouse_button(*argv, &message);
    if (message) {
        error = cmd_results_new(CMD_INVALID, message);
        free(message);
        return error;
    } else if (button == HAYWARD_SCROLL_UP || button == HAYWARD_SCROLL_DOWN || button == HAYWARD_SCROLL_LEFT || button == HAYWARD_SCROLL_RIGHT) {
        return cmd_results_new(
            CMD_INVALID, "X11 axis buttons are not supported for scroll_button"
        );
    } else if (!button) {
        return cmd_results_new(CMD_INVALID, "Unknown button %s", *argv);
    }
    ic->scroll_button = button;

    return cmd_results_new(CMD_SUCCESS, NULL);
}
