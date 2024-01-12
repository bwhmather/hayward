#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <libinput.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/profiler.h>
struct cmd_results *
input_cmd_click_method(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "click_method", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    if (strcasecmp(argv[0], "none") == 0) {
        ic->click_method = LIBINPUT_CONFIG_CLICK_METHOD_NONE;
    } else if (strcasecmp(argv[0], "button_areas") == 0) {
        ic->click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
    } else if (strcasecmp(argv[0], "clickfinger") == 0) {
        ic->click_method = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
    } else {
        return cmd_results_new(
            CMD_INVALID, "Expected 'click_method <none|button_areas|clickfinger>'"
        );
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
