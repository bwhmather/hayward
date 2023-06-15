#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <string.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/tree/arrange.h>

#include <config.h>

struct cmd_results *
cmd_hide_edge_borders(int argc, char **argv) {
    const char *expected_syntax =
        "Expected 'hide_edge_borders [--i3] "
        "none|vertical|horizontal|both";

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "hide_edge_borders", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    if (!argc) {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    if (strcmp(argv[0], "none") == 0) {
        config->hide_edge_borders = E_NONE;
    } else if (strcmp(argv[0], "vertical") == 0) {
        config->hide_edge_borders = E_VERTICAL;
    } else if (strcmp(argv[0], "horizontal") == 0) {
        config->hide_edge_borders = E_HORIZONTAL;
    } else if (strcmp(argv[0], "both") == 0) {
        config->hide_edge_borders = E_BOTH;
    } else {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }

    arrange_root(root);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
