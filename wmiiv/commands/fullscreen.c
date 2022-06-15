#include <strings.h>
#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "util.h"

// fullscreen [enable|disable|toggle] [global]
struct cmd_results *cmd_fullscreen(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "fullscreen", EXPECTED_AT_MOST, 2))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_FAILURE,
				"Can't run this command while there's no outputs connected.");
	}
	struct wmiiv_container *container = config->handler_context.container;

	if (!container) {
		// If the focus is not a container, do nothing successfully
		return cmd_results_new(CMD_SUCCESS, NULL);
	}

	bool is_fullscreen = container->pending.fullscreen_mode != FULLSCREEN_NONE;
	bool global = false;
	bool enable = !is_fullscreen;

	if (argc >= 1) {
		if (strcasecmp(argv[0], "global") == 0) {
			global = true;
		} else {
			enable = parse_boolean(argv[0], is_fullscreen);
		}
	}

	if (argc >= 2) {
		global = strcasecmp(argv[1], "global") == 0;
	}

	enum wmiiv_fullscreen_mode mode = FULLSCREEN_NONE;
	if (enable) {
		mode = global ? FULLSCREEN_GLOBAL : FULLSCREEN_WORKSPACE;
	}

	container_set_fullscreen(container, mode);
	arrange_root();

	return cmd_results_new(CMD_SUCCESS, NULL);
}