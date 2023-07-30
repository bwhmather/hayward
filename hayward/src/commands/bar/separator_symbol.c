#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdlib.h>
#include <string.h>

#include <hayward-common/log.h>

#include <hayward/config.h>

struct cmd_results *
bar_cmd_separator_symbol(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "separator_symbol", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    free(config->current_bar->separator_symbol);
    config->current_bar->separator_symbol = strdup(argv[0]);
    hwd_log(
        HWD_DEBUG, "Settings separator_symbol '%s' for bar: %s",
        config->current_bar->separator_symbol, config->current_bar->id
    );
    return cmd_results_new(CMD_SUCCESS, NULL);
}
