#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdlib.h>

#include <hayward/config.h>
#include <hayward/profiler.h>

struct cmd_results *
cmd_tiling_drag_threshold(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "tiling_drag_threshold", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    char *inv;
    int value = strtol(argv[0], &inv, 10);
    if (*inv != '\0' || value < 0) {
        return cmd_results_new(CMD_INVALID, "Invalid threshold specified");
    }

    config->tiling_drag_threshold = value;

    return cmd_results_new(CMD_SUCCESS, NULL);
}
