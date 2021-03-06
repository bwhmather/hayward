#include <string.h>
#include <strings.h>
#include "hayward/commands.h"
#include "log.h"
#include "util.h"

struct cmd_results *bar_cmd_wrap_scroll(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "wrap_scroll", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	config->current_bar->wrap_scroll =
			parse_boolean(argv[0], config->current_bar->wrap_scroll);
	if (config->current_bar->wrap_scroll) {
		hayward_log(HAYWARD_DEBUG, "Enabling wrap scroll on bar: %s",
			config->current_bar->id);
	} else {
		hayward_log(HAYWARD_DEBUG, "Disabling wrap scroll on bar: %s",
				config->current_bar->id);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
