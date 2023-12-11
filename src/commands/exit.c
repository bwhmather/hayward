#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

void
hwd_terminate(int exit_code);

struct cmd_results *
cmd_exit(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "exit", EXPECTED_EQUAL_TO, 0))) {
        return error;
    }
    hwd_terminate(0);
    return cmd_results_new(CMD_SUCCESS, NULL);
}
