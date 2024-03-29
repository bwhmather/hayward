#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <libinput.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/profiler.h>
struct cmd_results *
input_cmd_accel_profile(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "accel_profile", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    if (strcasecmp(argv[0], "adaptive") == 0) {
        ic->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    } else if (strcasecmp(argv[0], "flat") == 0) {
        ic->accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    } else {
        return cmd_results_new(CMD_INVALID, "Expected 'accel_profile <adaptive|flat>'");
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
