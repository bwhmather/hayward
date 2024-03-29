#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <libinput.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/profiler.h>
struct cmd_results *
input_cmd_scroll_method(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "scroll_method", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    if (strcasecmp(argv[0], "none") == 0) {
        ic->scroll_method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
    } else if (strcasecmp(argv[0], "two_finger") == 0) {
        ic->scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
    } else if (strcasecmp(argv[0], "edge") == 0) {
        ic->scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
    } else if (strcasecmp(argv[0], "on_button_down") == 0) {
        ic->scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
    } else {
        return cmd_results_new(
            CMD_INVALID, "Expected 'scroll_method <none|two_finger|edge|on_button_down>'"
        );
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
