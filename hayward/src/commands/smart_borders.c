#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <stdbool.h>
#include <string.h>

#include <hayward-common/util.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/tree/arrange.h>

#include <config.h>

struct cmd_results *
cmd_smart_borders(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "smart_borders", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    if (strcmp(argv[0], "no_gaps") == 0) {
        config->hide_edge_borders_smart = ESMART_NO_GAPS;
    } else {
        config->hide_edge_borders_smart =
            parse_boolean(argv[0], true) ? ESMART_ON : ESMART_OFF;
    }

    arrange_root(root);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
