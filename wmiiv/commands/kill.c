#include "log.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/commands.h"

static void close_window_iterator(struct wmiiv_container *window, void *data) {
	view_close(window->view);
}

struct cmd_results *cmd_kill(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct wmiiv_container *window = config->handler_context.window;
	struct wmiiv_workspace *workspace = config->handler_context.workspace;

	if (window) {
		close_window_iterator(window, NULL);
	} else {
		workspace_for_each_window(workspace, close_window_iterator, NULL);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
