#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <string.h>
#include <wayland-util.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_seat.h>

#include <hayward/config.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/log.h>
#include <hayward/server.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

struct cmd_results *
cmd_shortcuts_inhibitor(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "shortcuts_inhibitor", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    struct hwd_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_INVALID, "Only views can have shortcuts inhibitors");
    }

    struct hwd_view *view = window->view;
    if (strcmp(argv[0], "enable") == 0) {
        view->shortcuts_inhibit = SHORTCUTS_INHIBIT_ENABLE;
    } else if (strcmp(argv[0], "disable") == 0) {
        view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;

        struct hwd_seat *seat = NULL;
        wl_list_for_each(seat, &server.input->seats, link) {
            struct hwd_keyboard_shortcuts_inhibitor *hwd_inhibitor =
                keyboard_shortcuts_inhibitor_get_for_surface(seat, view->surface);
            if (!hwd_inhibitor) {
                continue;
            }

            wlr_keyboard_shortcuts_inhibitor_v1_deactivate(hwd_inhibitor->inhibitor);
            hwd_log(
                HWD_DEBUG,
                "Deactivated keyboard shortcuts "
                "inhibitor for seat %s on view",
                seat->wlr_seat->name
            );
        }
    } else {
        return cmd_results_new(CMD_INVALID, "Expected `shortcuts_inhibitor enable|disable`");
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
