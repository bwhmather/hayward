#include <string.h>
#include "wmiiv/commands.h"
#include "log.h"
#include "stringop.h"

struct cmd_results *cmd_wmiivbg_command(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "wmiivbg_command", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	free(config->wmiivbg_command);
	config->wmiivbg_command = NULL;

	char *new_command = join_args(argv, argc);
	if (strcmp(new_command, "-") != 0) {
		config->wmiivbg_command = new_command;
		wmiiv_log(SWAY_DEBUG, "Using custom wmiivbg command: %s",
				config->wmiivbg_command);
	} else {
		free(new_command);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
