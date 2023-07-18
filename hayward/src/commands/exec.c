#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <stdlib.h>

#include <hayward-common/log.h>
#include <hayward-common/stringop.h>

#include <hayward/config.h>

#include <config.h>

struct cmd_results *
cmd_exec(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = cmd_exec_validate(argc, argv))) {
        return error;
    }
    if (config->reloading) {
        char *args = join_args(argv, argc);
        hwd_log(HWD_DEBUG, "Ignoring 'exec %s' due to reload", args);
        free(args);
        return cmd_results_new(CMD_SUCCESS, NULL);
    }
    return cmd_exec_process(argc, argv);
}
