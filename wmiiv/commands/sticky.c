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
	struct wmiiv_container *container = config->handler_context.container;

	if (container == NULL) {
		return cmd_results_new(CMD_FAILURE, "No current container");
	};

	container->is_sticky = parse_boolean(argv[0], container->is_sticky);

	if (container_is_sticky_or_child(container)) {
		// move container to active workspace
		struct wmiiv_workspace *active_workspace =
			output_get_active_workspace(container->pending.workspace->output);
		if (!wmiiv_assert(active_workspace,
					"Expected output to have a workspace")) {
			return cmd_results_new(CMD_FAILURE,
					"Expected output to have a workspace");
		}
		if (container->pending.workspace != active_workspace) {
			struct wmiiv_workspace *old_workspace = container->pending.workspace;
			container_detach(container);
			workspace_add_floating(active_workspace, container);
			container_handle_fullscreen_reparent(container);
			arrange_workspace(active_workspace);
			workspace_consider_destroy(old_workspace);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
