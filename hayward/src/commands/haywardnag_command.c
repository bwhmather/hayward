#include <string.h>

#include "hayward-common/log.h"
#include "hayward-common/stringop.h"

#include "hayward/commands.h"

struct cmd_results *
cmd_haywardnag_command(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "haywardnag_command", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    free(config->haywardnag_command);
    config->haywardnag_command = NULL;

    char *new_command = join_args(argv, argc);
    if (strcmp(new_command, "-") != 0) {
        config->haywardnag_command = new_command;
        hayward_log(
            HAYWARD_DEBUG, "Using custom haywardnag command: %s",
            config->haywardnag_command
        );
    } else {
        free(new_command);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
