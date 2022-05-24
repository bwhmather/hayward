#include <string.h>
#include "wmiiv/commands.h"
#include "log.h"
#include "stringop.h"

struct cmd_results *cmd_wmiivnag_command(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "wmiivnag_command", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	free(config->wmiivnag_command);
	config->wmiivnag_command = NULL;

	char *new_command = join_args(argv, argc);
	if (strcmp(new_command, "-") != 0) {
		config->wmiivnag_command = new_command;
		wmiiv_log(WMIIV_DEBUG, "Using custom wmiivnag command: %s",
				config->wmiivnag_command);
	} else {
		free(new_command);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
