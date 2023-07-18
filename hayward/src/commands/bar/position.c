#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <hayward-common/log.h>

#include <hayward/config.h>

#include <config.h>

struct cmd_results *
bar_cmd_position(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "position", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    char *valid[] = {"top", "bottom"};
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        if (strcasecmp(valid[i], argv[0]) == 0) {
            hwd_log(
                HWD_DEBUG, "Setting bar position '%s' for bar: %s", argv[0], config->current_bar->id
            );
            free(config->current_bar->position);
            config->current_bar->position = strdup(argv[0]);
            return cmd_results_new(CMD_SUCCESS, NULL);
        }
    }
    return cmd_results_new(CMD_INVALID, "Invalid value %s", argv[0]);
}
