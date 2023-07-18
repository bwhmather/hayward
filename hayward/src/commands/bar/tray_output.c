#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/commands.h"

#include <stdlib.h>
#include <string.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>

#include <config.h>

struct cmd_results *
bar_cmd_tray_output(int argc, char **argv) {
#if HAVE_TRAY
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "tray_output", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    list_t *outputs = config->current_bar->tray_outputs;
    if (!outputs) {
        config->current_bar->tray_outputs = outputs = create_list();
    }

    if (strcmp(argv[0], "none") == 0) {
        hwd_log(HWD_DEBUG, "Hiding tray on bar: %s", config->current_bar->id);
        for (int i = 0; i < outputs->length; ++i) {
            free(outputs->items[i]);
        }
        outputs->length = 0;
    } else if (strcmp(argv[0], "*") == 0) {
        hwd_log(HWD_DEBUG, "Showing tray on all outputs for bar: %s", config->current_bar->id);
        while (outputs->length) {
            free(outputs->items[0]);
            list_del(outputs, 0);
        }
        return cmd_results_new(CMD_SUCCESS, NULL);
    } else {
        hwd_log(
            HWD_DEBUG, "Showing tray on output '%s' for bar: %s", argv[0], config->current_bar->id
        );
        if (outputs->length == 1 && strcmp(outputs->items[0], "none") == 0) {
            free(outputs->items[0]);
            list_del(outputs, 0);
        }
    }
    list_add(outputs, strdup(argv[0]));

    return cmd_results_new(CMD_SUCCESS, NULL);
#else
    return cmd_results_new(CMD_INVALID, "Hayward has been compiled without tray support");
#endif
}
