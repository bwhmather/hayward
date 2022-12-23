#include "hayward/commands.h"

#include <string.h>

#include <hayward-common/log.h>
#include <hayward-common/stringop.h>

struct cmd_results *
bar_cmd_haywardbar_command(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "haywardbar_command", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    free(config->current_bar->haywardbar_command);
    config->current_bar->haywardbar_command = join_args(argv, argc);
    hayward_log(
        HAYWARD_DEBUG, "Using custom haywardbar command: %s",
        config->current_bar->haywardbar_command
    );
    return cmd_results_new(CMD_SUCCESS, NULL);
}
