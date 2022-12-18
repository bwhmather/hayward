#include <string.h>
#include <strings.h>

#include "hayward-common/log.h"
#include "hayward-common/util.h"

#include "hayward/commands.h"

struct cmd_results *
bar_cmd_strip_workspace_numbers(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error =
             checkarg(argc, "strip_workspace_numbers", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    config->current_bar->strip_workspace_numbers =
        parse_boolean(argv[0], config->current_bar->strip_workspace_numbers);

    if (config->current_bar->strip_workspace_numbers) {
        config->current_bar->strip_workspace_name = false;

        hayward_log(
            HAYWARD_DEBUG, "Stripping workspace numbers on bar: %s",
            config->current_bar->id
        );
    } else {
        hayward_log(
            HAYWARD_DEBUG, "Enabling workspace numbers on bar: %s",
            config->current_bar->id
        );
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
