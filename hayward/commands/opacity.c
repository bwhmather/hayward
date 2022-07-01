#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include "hayward/commands.h"
#include "hayward/tree/view.h"
#include "hayward/desktop.h"
#include "log.h"

struct cmd_results *cmd_opacity(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "opacity", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	struct hayward_window *window = config->handler_context.window;

	if (window == NULL) {
		return cmd_results_new(CMD_FAILURE, "No current window");
	}

	char *err;
	float val = strtof(argc == 1 ? argv[0] : argv[1], &err);
	if (*err) {
		return cmd_results_new(CMD_INVALID, "opacity float invalid");
	}

	if (!strcasecmp(argv[0], "plus")) {
		val = window->alpha + val;
	} else if (!strcasecmp(argv[0], "minus")) {
		val = window->alpha - val;
	} else if (argc > 1 && strcasecmp(argv[0], "set")) {
		return cmd_results_new(CMD_INVALID,
				"Expected: set|plus|minus <0..1>: %s", argv[0]);
	}

	if (val < 0 || val > 1) {
		return cmd_results_new(CMD_FAILURE, "opacity value out of bounds");
	}

	window->alpha = val;
	desktop_damage_window(window);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
