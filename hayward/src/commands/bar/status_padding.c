#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdlib.h>
#include <string.h>

#include <hayward/config.h>
#include <hayward/log.h>

struct cmd_results *
bar_cmd_status_padding(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "status_padding", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    char *end;
    int padding = strtol(argv[0], &end, 10);
    if (strlen(end) || padding < 0) {
        return cmd_results_new(CMD_INVALID, "Padding must be a positive integer");
    }
    config->current_bar->status_padding = padding;
    hwd_log(
        HWD_DEBUG, "Status padding on bar %s: %d", config->current_bar->id,
        config->current_bar->status_padding
    );
    return cmd_results_new(CMD_SUCCESS, NULL);
}
