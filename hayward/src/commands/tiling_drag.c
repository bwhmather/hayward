#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <hayward-common/util.h>

#include <hayward/config.h>

struct cmd_results *
cmd_tiling_drag(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "tiling_drag", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    config->tiling_drag = parse_boolean(argv[0], config->tiling_drag);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
