#include "hayward-common/log.h"
#include "hayward/commands.h"
#include "hayward/input/seat.h"
#include "hayward/input/input-manager.h"
#include "hayward-common/util.h"

static struct cmd_results *handle_action(struct seat_config *sc,
		struct hayward_seat *seat, const char *action) {
	struct hayward_keyboard_shortcuts_inhibitor *hayward_inhibitor = NULL;
	if (strcmp(action, "disable") == 0) {
		sc->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;

		wl_list_for_each(hayward_inhibitor,
				&seat->keyboard_shortcuts_inhibitors, link) {
			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(
					hayward_inhibitor->inhibitor);
		}

		hayward_log(HAYWARD_DEBUG, "Deactivated all keyboard shortcuts inhibitors");
	} else {
		hayward_inhibitor = keyboard_shortcuts_inhibitor_get_for_focused_surface(seat);
		if (!hayward_inhibitor) {
			return cmd_results_new(CMD_FAILURE,
					"No inhibitor found for focused surface");
		}

		struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor =
			hayward_inhibitor->inhibitor;
		bool inhibit;
		if (strcmp(action, "activate") == 0) {
			inhibit = true;
		} else if (strcmp(action, "deactivate") == 0) {
			inhibit = false;
		} else if (strcmp(action, "toggle") == 0) {
			inhibit = !inhibitor->active;
		} else {
			return cmd_results_new(CMD_INVALID, "Expected enable|"
					"disable|activate|deactivate|toggle");
		}

		if (inhibit) {
			wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor);
		} else {
			wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor);
		}

		hayward_log(HAYWARD_DEBUG, "%sctivated keyboard shortcuts inhibitor",
				inhibit ? "A" : "Dea");
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

// shortcuts_inhibitor [enable|disable|activate|deactivate|toggle]
struct cmd_results *seat_cmd_shortcuts_inhibitor(int argc, char **argv) {
	struct cmd_results *error =
		checkarg(argc, "shortcuts_inhibitor", EXPECTED_EQUAL_TO, 1);
	if (error) {
		return error;
	}

	struct seat_config *sc = config->handler_context.seat_config;
	if (!sc) {
		return cmd_results_new(CMD_FAILURE, "No seat defined");
	}

	if (strcmp(argv[0], "enable") == 0) {
		sc->shortcuts_inhibit = SHORTCUTS_INHIBIT_ENABLE;
	// at runtime disable is an action that also deactivates all active
	// inhibitors handled in handle_action()
	} else if (strcmp(argv[0], "disable") == 0 && !config->active) {
		sc->shortcuts_inhibit = SHORTCUTS_INHIBIT_DISABLE;
	} else if (!config->active) {
		return cmd_results_new(CMD_INVALID, "only enable and disable "
				"can be used in the config");
	} else {
		if (strcmp(sc->name, "*") != 0) {
			struct hayward_seat *seat = input_manager_get_seat(sc->name, false);
			if (!seat) {
				return cmd_results_new(CMD_FAILURE,
						"Seat %s does not exist", sc->name);
			}
			error = handle_action(sc, seat, argv[0]);
		} else {
			struct hayward_seat *seat = NULL;
			wl_list_for_each(seat, &server.input->seats, link) {
				error = handle_action(sc, seat, argv[0]);
				if (error && error->status != CMD_SUCCESS) {
					break;
				}
			}
		}
	}

	return error ? error : cmd_results_new(CMD_SUCCESS, NULL);
}
