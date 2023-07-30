#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdlib.h>

#include <hayward/config.h>

struct cmd_results *
output_cmd_scale(int argc, char **argv) {
    if (!config->handler_context.output_config) {
        return cmd_results_new(CMD_FAILURE, "Missing output config");
    }
    if (!argc) {
        return cmd_results_new(CMD_INVALID, "Missing scale argument.");
    }

    char *end;
    config->handler_context.output_config->scale = strtof(*argv, &end);
    if (*end) {
        return cmd_results_new(CMD_INVALID, "Invalid scale.");
    }

    config->handler_context.leftovers.argc = argc - 1;
    config->handler_context.leftovers.argv = argv + 1;
    return NULL;
}
