#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>

#include <hayward-common/util.h>

#include <hayward/config.h>

struct cmd_results *
output_cmd_adaptive_sync(int argc, char **argv) {
    if (!config->handler_context.output_config) {
        return cmd_results_new(CMD_FAILURE, "Missing output config");
    }
    if (argc == 0) {
        return cmd_results_new(CMD_INVALID, "Missing adaptive_sync argument");
    }

    if (parse_boolean(argv[0], true)) {
        config->handler_context.output_config->adaptive_sync = 1;
    } else {
        config->handler_context.output_config->adaptive_sync = 0;
    }

    config->handler_context.leftovers.argc = argc - 1;
    config->handler_context.leftovers.argv = argv + 1;
    return NULL;
}
