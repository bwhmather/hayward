#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/stringop.h>

// Must be in order for the bsearch
static const struct cmd_handler mode_handlers[] = {
    {"bindcode", cmd_bindcode},     {"bindswitch", cmd_bindswitch},
    {"bindsym", cmd_bindsym},       {"set", cmd_set},
    {"unbindcode", cmd_unbindcode}, {"unbindswitch", cmd_unbindswitch},
    {"unbindsym", cmd_unbindsym},
};

struct cmd_results *
cmd_mode(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "mode", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    bool pango = strcmp(*argv, "--pango_markup") == 0;
    if (pango) {
        argc--;
        argv++;
        if (argc == 0) {
            return cmd_results_new(CMD_FAILURE, "Mode name is missing");
        }
    }

    if (config->reading && argc == 1) {
        return cmd_results_new(CMD_DEFER, NULL);
    }

    char *mode_name = *argv;
    strip_quotes(mode_name);
    struct hwd_mode *mode = NULL;
    // Find mode
    for (int i = 0; i < config->modes->length; ++i) {
        struct hwd_mode *test = config->modes->items[i];
        if (strcmp(test->name, mode_name) == 0) {
            mode = test;
            break;
        }
    }
    // Create mode if it doesn't exist
    if (!mode && argc > 1) {
        mode = calloc(1, sizeof(struct hwd_mode));
        if (!mode) {
            return cmd_results_new(CMD_FAILURE, "Unable to allocate mode");
        }
        mode->name = strdup(mode_name);
        mode->keysym_bindings = create_list();
        mode->keycode_bindings = create_list();
        mode->mouse_bindings = create_list();
        mode->switch_bindings = create_list();
        mode->pango = pango;
        list_add(config->modes, mode);
    }
    if (!mode) {
        error = cmd_results_new(CMD_INVALID, "Unknown mode `%s'", mode_name);
        return error;
    }
    // Set current mode
    struct hwd_mode *stored_mode = config->current_mode;
    config->current_mode = mode;
    if (argc == 1) {
        // trigger IPC mode event
        wlr_log(WLR_DEBUG, "Switching to mode `%s' (pango=%d)", mode->name, mode->pango);
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    // Create binding
    struct cmd_results *result =
        config_subcommand(argv + 1, argc - 1, mode_handlers, sizeof(mode_handlers));
    config->current_mode = stored_mode;

    return result;
}
