#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include <hayward/commands.h>
#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/tree/arrange.h>

#include <config.h>

struct cmd_results *
cmd_titlebar_border_thickness(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error =
             checkarg(argc, "titlebar_border_thickness", EXPECTED_EQUAL_TO, 1)
        )) {
        return error;
    }

    char *inv;
    int value = strtol(argv[0], &inv, 10);
    if (*inv != '\0' || value < 0 || value > config->titlebar_v_padding) {
        return cmd_results_new(CMD_FAILURE, "Invalid size specified");
    }

    config->titlebar_border_thickness = value;

    arrange_root(root);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
