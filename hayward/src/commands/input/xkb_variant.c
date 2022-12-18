#define _POSIX_C_SOURCE 200809L
#include "hayward-common/log.h"

#include "hayward/commands.h"
#include "hayward/config.h"

struct cmd_results *
input_cmd_xkb_variant(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "xkb_variant", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    ic->xkb_variant = strdup(argv[0]);

    hayward_log(
        HAYWARD_DEBUG, "set-xkb_variant for config: %s variant: %s",
        ic->identifier, ic->xkb_variant
    );
    return cmd_results_new(CMD_SUCCESS, NULL);
}
