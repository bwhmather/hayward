#include "hayward/commands.h"

#include <string.h>

#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

struct cmd_results *
cmd_shortcuts_inhibitor(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "shortcuts_inhibitor", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    struct hayward_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(
            CMD_INVALID, "Only views can have shortcuts inhibitors"
        );
    }

    struct hayward_view *view = window->view;
    if (strcmp(argv[0], "enable") == 0) {
        view->shortcuts_inhibit = SHORTCUTS_INHIBIT_ENABLE;
    } else if (strcmp(argv[0], "disable") == 0) {
        view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;

        struct hayward_seat *seat = NULL;
        wl_list_for_each(seat, &server.input->seats, link) {
            struct hayward_keyboard_shortcuts_inhibitor *hayward_inhibitor =
                keyboard_shortcuts_inhibitor_get_for_surface(
                    seat, view->surface
                );
            if (!hayward_inhibitor) {
                continue;
            }

            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(
                hayward_inhibitor->inhibitor
            );
            hayward_log(
                HAYWARD_DEBUG,
                "Deactivated keyboard shortcuts "
                "inhibitor for seat %s on view",
                seat->wlr_seat->name
            );
        }
    } else {
        return cmd_results_new(
            CMD_INVALID, "Expected `shortcuts_inhibitor enable|disable`"
        );
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
