#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>
#include <string.h>

#include <wayland-util.h>

#include <wlr/types/wlr_seat.h>

#include <hayward/config.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/profiler.h>
#include <hayward/server.h>

enum operation {
    OP_ENABLE,
    OP_DISABLE,
    OP_ESCAPE,
};

// pointer_constraint [enable|disable|escape]
struct cmd_results *
seat_cmd_pointer_constraint(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "pointer_constraint", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    if (!config->handler_context.seat_config) {
        return cmd_results_new(CMD_FAILURE, "No seat defined");
    }

    enum operation op;
    if (strcmp(argv[0], "enable") == 0) {
        op = OP_ENABLE;
    } else if (strcmp(argv[0], "disable") == 0) {
        op = OP_DISABLE;
    } else if (strcmp(argv[0], "escape") == 0) {
        op = OP_ESCAPE;
    } else {
        return cmd_results_new(CMD_FAILURE, "Expected enable|disable|escape");
    }

    if (op == OP_ESCAPE && config->reading) {
        return cmd_results_new(CMD_FAILURE, "Can only escape at runtime.");
    }

    struct seat_config *seat_config = config->handler_context.seat_config;
    switch (op) {
    case OP_ENABLE:
        seat_config->allow_constrain = CONSTRAIN_ENABLE;
        break;
    case OP_DISABLE:
        seat_config->allow_constrain = CONSTRAIN_DISABLE;
        /* fallthrough */
    case OP_ESCAPE:;
        bool wildcard = !strcmp(seat_config->name, "*");
        struct hwd_seat *seat = NULL;
        wl_list_for_each(seat, &server.input->seats, link) {
            if (wildcard || !strcmp(seat->wlr_seat->name, seat_config->name)) {
                hwd_cursor_constrain(seat->cursor, NULL);
            }
        }
        break;
    }
    return cmd_results_new(CMD_SUCCESS, NULL);
}
