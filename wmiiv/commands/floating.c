#include <string.h>
#include <strings.h>
#include "wmiiv/commands.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "list.h"
#include "util.h"

struct cmd_results *cmd_floating(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "floating", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct wmiiv_container *window = config->handler_context.window;
	if (!window) {
		return cmd_results_new(CMD_INVALID, "Can only float windows");
	}

	bool wants_floating =
		parse_boolean(argv[0], window_is_floating(window));

	window_set_floating(window, wants_floating);

	arrange_workspace(window->pending.workspace);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
