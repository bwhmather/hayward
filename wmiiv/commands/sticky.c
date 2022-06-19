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
#include "log.h"
#include "util.h"

struct cmd_results *cmd_sticky(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "sticky", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	struct wmiiv_container *window = config->handler_context.window;

	if (window == NULL) {
		return cmd_results_new(CMD_FAILURE, "No current window");
	};

	window->is_sticky = parse_boolean(argv[0], window->is_sticky);

	if (window_is_sticky(window)) {
		// move window to active workspace
		struct wmiiv_workspace *active_workspace =
			output_get_active_workspace(window->pending.workspace->output);
		if (!wmiiv_assert(active_workspace,
					"Expected output to have a workspace")) {
			return cmd_results_new(CMD_FAILURE,
					"Expected output to have a workspace");
		}
		if (window->pending.workspace != active_workspace) {
			struct wmiiv_workspace *old_workspace = window->pending.workspace;
			window_detach(window);
			workspace_add_floating(active_workspace, window);
			window_handle_fullscreen_reparent(window);
			arrange_workspace(active_workspace);
			workspace_consider_destroy(old_workspace);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
