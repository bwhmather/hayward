#include <string.h>

#include "hayward-common/log.h"
#include "hayward-common/stringop.h"

#include "hayward/commands.h"

struct cmd_results *cmd_haywardbg_command(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "haywardbg_command", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    free(config->haywardbg_command);
    config->haywardbg_command = NULL;

    char *new_command = join_args(argv, argc);
    if (strcmp(new_command, "-") != 0) {
        config->haywardbg_command = new_command;
        hayward_log(
            HAYWARD_DEBUG, "Using custom haywardbg command: %s",
            config->haywardbg_command
        );
    } else {
        free(new_command);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
