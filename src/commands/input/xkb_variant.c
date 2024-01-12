#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <string.h>

#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/profiler.h>

struct cmd_results *
input_cmd_xkb_variant(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "xkb_variant", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    ic->xkb_variant = strdup(argv[0]);

    wlr_log(
        WLR_DEBUG, "set-xkb_variant for config: %s variant: %s", ic->identifier, ic->xkb_variant
    );
    return cmd_results_new(CMD_SUCCESS, NULL);
}
