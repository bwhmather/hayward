#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <math.h>

#include <hayward-common/util.h>

#include <hayward/config.h>

#include <config.h>

struct cmd_results *
input_cmd_pointer_accel(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "pointer_accel", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    float pointer_accel = parse_float(argv[0]);
    if (isnan(pointer_accel)) {
        return cmd_results_new(CMD_INVALID, "Invalid pointer accel; expected float.");
    }
    if (pointer_accel < -1 || pointer_accel > 1) {
        return cmd_results_new(CMD_INVALID, "Input out of range [-1, 1]");
    }
    ic->pointer_accel = pointer_accel;

    return cmd_results_new(CMD_SUCCESS, NULL);
}
