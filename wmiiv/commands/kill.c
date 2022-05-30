#include "log.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/commands.h"

static void close_container_iterator(struct wmiiv_container *container, void *data) {
	if (container->view) {
		view_close(container->view);
	}
}

struct cmd_results *cmd_kill(int argc, char **argv) {
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct wmiiv_container *container = config->handler_context.container;
	struct wmiiv_workspace *workspace = config->handler_context.workspace;

	if (container) {
		close_container_iterator(container, NULL);
		container_for_each_child(container, close_container_iterator, NULL);
	} else {
		workspace_for_each_container(workspace, close_container_iterator, NULL);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
