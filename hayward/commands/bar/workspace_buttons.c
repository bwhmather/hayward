#include <string.h>
#include <strings.h>
#include "hayward/commands.h"
#include "log.h"
#include "util.h"

struct cmd_results *bar_cmd_workspace_buttons(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "workspace_buttons", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	config->current_bar->workspace_buttons =
		parse_boolean(argv[0], config->current_bar->workspace_buttons);
	if (config->current_bar->workspace_buttons) {
		hayward_log(HAYWARD_DEBUG, "Enabling workspace buttons on bar: %s",
				config->current_bar->id);
	} else {
		hayward_log(HAYWARD_DEBUG, "Disabling workspace buttons on bar: %s",
				config->current_bar->id);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
