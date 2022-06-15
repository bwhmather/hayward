#include <assert.h>
#include <stdlib.h>
#include <strings.h>
#include "wmiiv/commands.h"
#include "wmiiv/tree/view.h"
#include "log.h"

struct cmd_results *cmd_opacity(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "opacity", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	struct wmiiv_container *container = config->handler_context.container;

	if (container == NULL) {
		return cmd_results_new(CMD_FAILURE, "No current container");
	}

	char *err;
	float val = strtof(argc == 1 ? argv[0] : argv[1], &err);
	if (*err) {
		return cmd_results_new(CMD_INVALID, "opacity float invalid");
	}

	if (!strcasecmp(argv[0], "plus")) {
		val = container->alpha + val;
	} else if (!strcasecmp(argv[0], "minus")) {
		val = container->alpha - val;
	} else if (argc > 1 && strcasecmp(argv[0], "set")) {
		return cmd_results_new(CMD_INVALID,
				"Expected: set|plus|minus <0..1>: %s", argv[0]);
	}

	if (val < 0 || val > 1) {
		return cmd_results_new(CMD_FAILURE, "opacity value out of bounds");
	}

	container->alpha = val;
	container_damage_whole(container);
	return cmd_results_new(CMD_SUCCESS, NULL);
}