#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include <hayward/commands.h>
#include <hayward/config.h>

#include <config.h>

struct cmd_results *
input_cmd_repeat_rate(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "repeat_rate", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    int repeat_rate = atoi(argv[0]);
    if (repeat_rate < 0) {
        return cmd_results_new(CMD_INVALID, "Repeat rate cannot be negative");
    }
    ic->repeat_rate = repeat_rate;

    return cmd_results_new(CMD_SUCCESS, NULL);
}
