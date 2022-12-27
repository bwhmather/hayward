#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/commands.h"

#include <string.h>

#include <hayward-common/list.h>

#include <hayward/config.h>

#include <config.h>

struct cmd_results *
seat_cmd_attach(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "attach", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    if (!config->handler_context.seat_config) {
        return cmd_results_new(CMD_FAILURE, "No seat defined");
    }
    if (!config->active) {
        return cmd_results_new(CMD_DEFER, NULL);
    }

    struct seat_attachment_config *attachment = seat_attachment_config_new();
    if (!attachment) {
        return cmd_results_new(
            CMD_FAILURE, "Failed to allocate seat attachment config"
        );
    }
    attachment->identifier = strdup(argv[0]);
    list_add(config->handler_context.seat_config->attachments, attachment);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
