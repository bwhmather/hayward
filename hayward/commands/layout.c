#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include "hayward/commands.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/window.h"
#include "hayward/tree/workspace.h"
#include "log.h"

static enum hayward_column_layout parse_layout_string(char *s) {
	if (strcasecmp(s, "split") == 0) {
		return L_SPLIT;
	} else if (strcasecmp(s, "stacking") == 0) {
		return L_STACKED;
	}
	return L_NONE;
}

static const char expected_syntax[] =
	"Expected 'layout default|stacking|split'";

struct cmd_results *cmd_layout(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "layout", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct hayward_window *window = config->handler_context.window;
	struct hayward_workspace *workspace = config->handler_context.workspace;

	if (!window) {
		return cmd_results_new(CMD_INVALID, "No window selected");
	}

	if (window_is_floating(window)) {
		return cmd_results_new(CMD_FAILURE, "Unable to change the layout of floating containers");
	}

	struct hayward_column *column = window->pending.parent;
	if (!column) {
		return cmd_results_new(CMD_FAILURE, "Window is not a member of a column");
	}


	enum hayward_column_layout new_layout = L_NONE;
	enum hayward_column_layout old_layout = L_NONE;

	old_layout = column->pending.layout;
	new_layout = parse_layout_string(argv[0]);

	if (new_layout == L_NONE) {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (new_layout != old_layout) {
		column->pending.layout = new_layout;
		if (root->fullscreen_global) {
			arrange_root();
		} else {
			arrange_workspace(workspace);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
