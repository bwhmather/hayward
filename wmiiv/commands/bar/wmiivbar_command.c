#include <string.h>
#include "wmiiv/commands.h"
#include "log.h"
#include "stringop.h"

struct cmd_results *bar_cmd_wmiivbar_command(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "wmiivbar_command", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	free(config->current_bar->wmiivbar_command);
	config->current_bar->wmiivbar_command = join_args(argv, argc);
	wmiiv_log(SWAY_DEBUG, "Using custom wmiivbar command: %s",
			config->current_bar->wmiivbar_command);
	return cmd_results_new(CMD_SUCCESS, NULL);
}
