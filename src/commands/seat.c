#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <string.h>

#include <wlr/types/wlr_seat.h>

#include <hayward/config.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/profiler.h>

// must be in order for the bsearch
// these handlers alter the seat config
static const struct cmd_handler seat_handlers[] = {
    {"attach", seat_cmd_attach},
    {"fallback", seat_cmd_fallback},
    {"hide_cursor", seat_cmd_hide_cursor},
    {"idle_inhibit", seat_cmd_idle_inhibit},
    {"idle_wake", seat_cmd_idle_wake},
    {"keyboard_grouping", seat_cmd_keyboard_grouping},
    {"pointer_constraint", seat_cmd_pointer_constraint},
    {"xcursor_theme", seat_cmd_xcursor_theme},
};

static struct cmd_results *
config_handlers(int argc, char **argv) {
    struct cmd_results *res = config_subcommand(argv, argc, seat_handlers, sizeof(seat_handlers));
    if (res && res->status != CMD_SUCCESS) {
        free_seat_config(config->handler_context.seat_config);
    } else {
        struct seat_config *sc = store_seat_config(config->handler_context.seat_config);
        if (!config->reading) {
            input_manager_apply_seat_config(sc);
        }
    }
    config->handler_context.seat_config = NULL;
    return res;
}

struct cmd_results *
cmd_seat(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "seat", EXPECTED_AT_LEAST, 2))) {
        return error;
    }

    if (!strcmp(argv[0], "-")) {
        if (config->reading) {
            return cmd_results_new(
                CMD_FAILURE, "Current seat alias (-) cannot be used in the config"
            );
        }
        config->handler_context.seat_config =
            new_seat_config(config->handler_context.seat->wlr_seat->name);
    } else {
        config->handler_context.seat_config = new_seat_config(argv[0]);
    }
    if (!config->handler_context.seat_config) {
        return cmd_results_new(CMD_FAILURE, "Couldn't allocate config");
    }

    struct cmd_results *res = config_handlers(argc - 1, argv + 1);
    return res ? res : cmd_results_new(CMD_SUCCESS, NULL);
}
