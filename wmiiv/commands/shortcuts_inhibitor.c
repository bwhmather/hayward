#include <string.h>
#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"

struct cmd_results *cmd_shortcuts_inhibitor(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "shortcuts_inhibitor", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct wmiiv_container *container = config->handler_context.container;
	if (!container || !container->view) {
		return cmd_results_new(CMD_INVALID,
				"Only views can have shortcuts inhibitors");
	}

	struct wmiiv_view *view = container->view;
	if (strcmp(argv[0], "enable") == 0) {
		view->shortcuts_inhibit = SHORTCUTS_INHIBIT_ENABLE;
	} else if (strcmp(argv[0], "disable") == 0) {
		view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;

		struct wmiiv_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct wmiiv_keyboard_shortcuts_inhibitor *wmiiv_inhibitor =
				keyboard_shortcuts_inhibitor_get_for_surface(
						seat, view->surface);
			if (!wmiiv_inhibitor) {
				continue;
			}

			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(
					wmiiv_inhibitor->inhibitor);
			wmiiv_log(WMIIV_DEBUG, "Deactivated keyboard shortcuts "
					"inhibitor for seat %s on view",
					seat->wlr_seat->name);

		}
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected `shortcuts_inhibitor enable|disable`");
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}