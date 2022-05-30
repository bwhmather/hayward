#include <string.h>
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/desktop/idle_inhibit_v1.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"

struct cmd_results *cmd_inhibit_idle(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "inhibit_idle", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}

	struct wmiiv_container *container = config->handler_context.container;
	if (!container || !container->view) {
		return cmd_results_new(CMD_INVALID,
				"Only views can have idle inhibitors");
	}

	bool clear = false;
	enum wmiiv_idle_inhibit_mode mode;
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

	struct wmiiv_idle_inhibitor_v1 *inhibitor =
		wmiiv_idle_inhibit_v1_user_inhibitor_for_view(container->view);
	if (inhibitor) {
		if (clear) {
			wmiiv_idle_inhibit_v1_user_inhibitor_destroy(inhibitor);
		} else {
			inhibitor->mode = mode;
			wmiiv_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
		}
	} else if (!clear) {
		wmiiv_idle_inhibit_v1_user_inhibitor_register(container->view, mode);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
