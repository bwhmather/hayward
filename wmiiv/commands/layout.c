#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include "wmiiv/commands.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/workspace.h"
#include "log.h"

static enum wmiiv_container_layout parse_layout_string(char *s) {
	if (strcasecmp(s, "split") == 0) {
		return L_VERT;
	} else if (strcasecmp(s, "stacking") == 0) {
		return L_STACKED;
	}
	return L_NONE;
}

static const char expected_syntax[] =
	"Expected 'layout default|tabbed|stacking|splitv|splith' or "
	"'layout toggle [split|all]' or "
	"'layout toggle [split|tabbed|stacking|splitv|splith] [split|tabbed|stacking|splitv|splith]...'";

struct cmd_results *cmd_layout(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "layout", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	struct wmiiv_container *window = config->handler_context.container;
	struct wmiiv_workspace *workspace = config->handler_context.workspace;

	if (!window) {
		return cmd_results_new(CMD_INVALID, "No window selected");
	}

	if (!container_is_window(window)) {
		return cmd_results_new(CMD_FAILURE, "Can only run this command on a window");
	}

	if (window_is_floating(window)) {
		return cmd_results_new(CMD_FAILURE, "Unable to change the layout of floating containers");
	}

	struct wmiiv_container *column = window->pending.parent;
	if (!column) {
		return cmd_results_new(CMD_FAILURE, "Window is not a member of a column");
	}


	enum wmiiv_container_layout new_layout = L_NONE;
	enum wmiiv_container_layout old_layout = L_NONE;

	old_layout = column->pending.layout;
	new_layout = parse_layout_string(argv[0]);

	if (new_layout == L_NONE) {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}

	if (new_layout != old_layout) {
		column->pending.layout = new_layout;
		container_update_representation(column);
		if (root->fullscreen_global) {
			arrange_root();
		} else {
			arrange_workspace(workspace);
		}
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
