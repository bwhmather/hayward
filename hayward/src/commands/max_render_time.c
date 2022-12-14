#include <strings.h>
#include "hayward/commands.h"
#include "hayward/config.h"
#include "hayward/tree/view.h"

struct cmd_results *cmd_max_render_time(int argc, char **argv) {
	if (!argc) {
		return cmd_results_new(CMD_INVALID, "Missing max render time argument.");
	}

	int max_render_time;
	if (!strcmp(*argv, "off")) {
		max_render_time = 0;
	} else {
		char *end;
		max_render_time = strtol(*argv, &end, 10);
		if (*end || max_render_time <= 0) {
			return cmd_results_new(CMD_INVALID, "Invalid max render time.");
		}
	}

	struct hayward_window *window = config->handler_context.window;
	if (!window) {
		return cmd_results_new(CMD_INVALID,
				"Only views can have a max_render_time");
	}

	struct hayward_view *view = window->view;
	view->max_render_time = max_render_time;

	return cmd_results_new(CMD_SUCCESS, NULL);
}
