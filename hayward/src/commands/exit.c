#include <stddef.h>

#include "hayward/commands.h"
#include "hayward/config.h"

void
hayward_terminate(int exit_code);

struct cmd_results *
cmd_exit(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "exit", EXPECTED_EQUAL_TO, 0))) {
        return error;
    }
    hayward_terminate(0);
    return cmd_results_new(CMD_SUCCESS, NULL);
}
