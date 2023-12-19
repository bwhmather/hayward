#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/stringop.h>

struct cmd_results *
input_cmd_xkb_file(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "xkb_file", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    struct input_config *ic = config->handler_context.input_config;
    if (!ic) {
        return cmd_results_new(CMD_FAILURE, "No input device defined.");
    }

    if (strcmp(argv[0], "-") == 0) {
        free(ic->xkb_file);
        ic->xkb_file = NULL;
    } else {
        ic->xkb_file = strdup(argv[0]);
        if (!expand_path(&ic->xkb_file)) {
            error = cmd_results_new(CMD_INVALID, "Invalid syntax (%s)", ic->xkb_file);
            free(ic->xkb_file);
            ic->xkb_file = NULL;
            return error;
        }
        if (!ic->xkb_file) {
            wlr_log(WLR_ERROR, "Failed to allocate expanded path");
            return cmd_results_new(CMD_FAILURE, "Unable to allocate resource");
        }

        bool can_access = access(ic->xkb_file, F_OK) != -1;
        if (!can_access) {
            wlr_log_errno(WLR_ERROR, "Unable to access xkb file '%s'", ic->xkb_file);
            config_add_haywardnag_warning("Unable to access xkb file '%s'", ic->xkb_file);
        }
    }
    ic->xkb_file_is_set = true;

    wlr_log(WLR_DEBUG, "set-xkb_file for config: %s file: %s", ic->identifier, ic->xkb_file);
    return cmd_results_new(CMD_SUCCESS, NULL);
}
