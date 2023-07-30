#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>

#include <hayward-common/log.h>
#include <hayward-common/util.h>

#include <hayward/config.h>

struct cmd_results *
bar_cmd_strip_workspace_name(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "strip_workspace_name", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    config->current_bar->strip_workspace_name =
        parse_boolean(argv[0], config->current_bar->strip_workspace_name);

    if (config->current_bar->strip_workspace_name) {
        config->current_bar->strip_workspace_numbers = false;

        hwd_log(HWD_DEBUG, "Stripping workspace name on bar: %s", config->current_bar->id);
    } else {
        hwd_log(HWD_DEBUG, "Enabling workspace name on bar: %s", config->current_bar->id);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
