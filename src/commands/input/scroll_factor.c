#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <math.h>

#include <hayward/config.h>
#include <hayward/profiler.h>
#include <hayward/util.h>
struct cmd_results *
input_cmd_scroll_factor(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "scroll_factor", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    float scroll_factor = parse_float(argv[0]);
    if (isnan(scroll_factor)) {
        return cmd_results_new(CMD_INVALID, "Invalid scroll factor; expected float.");
    } else if (scroll_factor < 0) {
        return cmd_results_new(CMD_INVALID, "Scroll factor cannot be negative.");
    }
    ic->scroll_factor = scroll_factor;

    return cmd_results_new(CMD_SUCCESS, NULL);
}
