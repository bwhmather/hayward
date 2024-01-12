#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/profiler.h>
#include <hayward/stringop.h>

struct cmd_results *
cmd_haywardnag_command(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "haywardnag_command", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    free(config->haywardnag_command);
    config->haywardnag_command = NULL;

    char *new_command = join_args(argv, argc);
    if (strcmp(new_command, "-") != 0) {
        config->haywardnag_command = new_command;
        wlr_log(WLR_DEBUG, "Using custom haywardnag command: %s", config->haywardnag_command);
    } else {
        free(new_command);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
