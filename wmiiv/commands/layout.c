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
	if (strcasecmp(s, "splith") == 0) {
		return L_HORIZ;
	} else if (strcasecmp(s, "splitv") == 0) {
		return L_VERT;
	} else if (strcasecmp(s, "tabbed") == 0) {
		return L_TABBED;
	} else if (strcasecmp(s, "stacking") == 0) {
		return L_STACKED;
	}
	return L_NONE;
}

static const char expected_syntax[] =
	"Expected 'layout default|tabbed|stacking|splitv|splith' or "
	"'layout toggle [split|all]' or "
	"'layout toggle [split|tabbed|stacking|splitv|splith] [split|tabbed|stacking|splitv|splith]...'";

static enum wmiiv_container_layout toggle_split_layout(
		enum wmiiv_container_layout layout,
		enum wmiiv_container_layout prev_split_layout,
		struct wmiiv_output *output) {
	if (layout == L_HORIZ) {
		return L_VERT;
	} else if (layout == L_VERT) {
		return L_HORIZ;
	} else if (prev_split_layout != L_NONE) {
		return prev_split_layout;
	} else if (output->height > output->width) {
		return L_VERT;
	}
	return L_HORIZ;
}

static enum wmiiv_container_layout get_layout_toggle(int argc, char **argv,
		enum wmiiv_container_layout layout,
		enum wmiiv_container_layout prev_split_layout,
		struct wmiiv_output *output) {
	// "layout toggle"
	if (argc == 1) {
		return toggle_split_layout(layout, prev_split_layout, output);
	}

	if (argc == 2) {
		// "layout toggle split" (same as "layout toggle")
		if (strcasecmp(argv[1], "split") == 0) {
			return toggle_split_layout(layout, prev_split_layout, output);
		}
		// "layout toggle all"
		if (strcasecmp(argv[1], "all") == 0) {
			return layout == L_HORIZ ? L_VERT :
				layout == L_VERT ? L_STACKED :
				layout == L_STACKED ? L_TABBED : L_HORIZ;
		}
		return L_NONE;
	}

	enum wmiiv_container_layout parsed;
	int curr = 1;
	for (; curr < argc; curr++) {
		parsed = parse_layout_string(argv[curr]);
		if (parsed == layout || (strcmp(argv[curr], "split") == 0 &&
				 (layout == L_VERT || layout == L_HORIZ))) {
			break;
		}
	}
	for (int i = curr + 1; i != curr; ++i) {
		// cycle round to find next valid layout
		if (i >= argc) {
			i = 1;
		}
		parsed = parse_layout_string(argv[i]);
		if (parsed != L_NONE) {
			return parsed;
		}
		if (strcmp(argv[i], "split") == 0) {
			return toggle_split_layout(layout, prev_split_layout, output);
		}
		// invalid layout strings are silently ignored
	}
	return L_NONE;
}

static enum wmiiv_container_layout get_layout(int argc, char **argv,
		enum wmiiv_container_layout layout,
		enum wmiiv_container_layout prev_split_layout,
		struct wmiiv_output *output) {
	// Check if assigned directly
	enum wmiiv_container_layout parsed = parse_layout_string(argv[0]);
	if (parsed != L_NONE) {
		return parsed;
	}

	if (strcasecmp(argv[0], "default") == 0) {
		return prev_split_layout;
	}

	if (strcasecmp(argv[0], "toggle") == 0) {
		return get_layout_toggle(argc, argv, layout, prev_split_layout, output);
	}

	return L_NONE;
}

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
	new_layout = get_layout(argc, argv,
			column->pending.layout, column->prev_split_layout,
			column->pending.workspace->output);

	if (new_layout == L_NONE) {
		return cmd_results_new(CMD_INVALID, expected_syntax);
	}
	if (new_layout != old_layout) {
		if (old_layout != L_TABBED && old_layout != L_STACKED) {
			column->prev_split_layout = old_layout;
		}
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
