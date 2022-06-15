#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "util.h"

struct cmd_results *cmd_urgent(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "urgent", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	struct wmiiv_container *container = config->handler_context.container;
	if (!container) {
		return cmd_results_new(CMD_FAILURE, "No current container");
	}
	if (!container->view) {
		return cmd_results_new(CMD_INVALID, "Only views can be urgent");
	}
	struct wmiiv_view *view = container->view;

	if (strcmp(argv[0], "allow") == 0) {
		view->allow_request_urgent = true;
	} else if (strcmp(argv[0], "deny") == 0) {
		view->allow_request_urgent = false;
	} else {
		view_set_urgent(view, parse_boolean(argv[0], view_is_urgent(view)));
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}