#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output.h>

#include <hayward-common/util.h>

#include <hayward/config.h>
#include <hayward/output.h>

#include <config.h>

struct cmd_results *
output_cmd_dpms(int argc, char **argv) {
    if (!config->handler_context.output_config) {
        return cmd_results_new(CMD_FAILURE, "Missing output config");
    }
    if (!argc) {
        return cmd_results_new(CMD_INVALID, "Missing dpms argument.");
    }

    enum config_dpms current_dpms = DPMS_ON;

    if (strcasecmp(argv[0], "toggle") == 0) {

        const char *oc_name = config->handler_context.output_config->name;
        if (strcmp(oc_name, "*") == 0) {
            return cmd_results_new(
                CMD_INVALID, "Cannot apply toggle to all outputs."
            );
        }

        struct hayward_output *hayward_output =
            all_output_by_name_or_id(oc_name);
        if (!hayward_output || !hayward_output->wlr_output) {
            return cmd_results_new(
                CMD_FAILURE, "Cannot apply toggle to unknown output %s", oc_name
            );
        }

        if (hayward_output->enabled && !hayward_output->wlr_output->enabled) {
            current_dpms = DPMS_OFF;
        }
    }

    if (parse_boolean(argv[0], current_dpms == DPMS_ON)) {
        config->handler_context.output_config->dpms_state = DPMS_ON;
    } else {
        config->handler_context.output_config->dpms_state = DPMS_OFF;
    }

    config->handler_context.leftovers.argc = argc - 1;
    config->handler_context.leftovers.argv = argv + 1;
    return NULL;
}
