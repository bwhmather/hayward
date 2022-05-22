#include <string.h>
#include <strings.h>
#include "sway/commands.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/tree/arrange.h"
#include "sway/tree/container.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
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
	struct sway_container *win = config->handler_context.window;
	if (!win) {
		return cmd_results_new(CMD_INVALID, "Can only float windows");
	}

	bool wants_floating =
		parse_boolean(argv[0], container_is_floating(win));

	window_set_floating(win, wants_floating);

	arrange_workspace(win->pending.workspace);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
