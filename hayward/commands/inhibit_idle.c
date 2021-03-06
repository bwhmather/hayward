#include <string.h>
#include "hayward/commands.h"
#include "hayward/config.h"
#include "hayward/desktop/idle_inhibit_v1.h"
#include "hayward/tree/window.h"
#include "hayward/tree/view.h"

struct cmd_results *cmd_inhibit_idle(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "inhibit_idle", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct hayward_window *window = config->handler_context.window;
	if (!window) {
		return cmd_results_new(CMD_INVALID,
				"Only views can have idle inhibitors");
	}

	bool clear = false;
	enum hayward_idle_inhibit_mode mode;
	if (strcmp(argv[0], "focus") == 0) {
		mode = INHIBIT_IDLE_FOCUS;
	} else if (strcmp(argv[0], "fullscreen") == 0) {
		mode = INHIBIT_IDLE_FULLSCREEN;
	} else if (strcmp(argv[0], "open") == 0) {
		mode = INHIBIT_IDLE_OPEN;
	} else if (strcmp(argv[0], "none") == 0) {
		clear = true;
	} else if (strcmp(argv[0], "visible") == 0) {
		mode = INHIBIT_IDLE_VISIBLE;
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected `inhibit_idle focus|fullscreen|open|none|visible`");
	}

	struct hayward_idle_inhibitor_v1 *inhibitor =
		hayward_idle_inhibit_v1_user_inhibitor_for_view(window->view);
	if (inhibitor) {
		if (clear) {
			hayward_idle_inhibit_v1_user_inhibitor_destroy(inhibitor);
		} else {
			inhibitor->mode = mode;
			hayward_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
		}
	} else if (!clear) {
		hayward_idle_inhibit_v1_user_inhibitor_register(window->view, mode);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
