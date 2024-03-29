#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <libinput.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/profiler.h>
struct cmd_results *
input_cmd_tap_button_map(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "tap_button_map", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    if (strcasecmp(argv[0], "lrm") == 0) {
        ic->tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
    } else if (strcasecmp(argv[0], "lmr") == 0) {
        ic->tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LMR;
    } else {
        return cmd_results_new(CMD_INVALID, "Expected 'tap_button_map <lrm|lmr>'");
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
