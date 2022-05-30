#include <string.h>
#include <strings.h>
#include "wmiiv/commands.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"
#include "log.h"

static struct cmd_results *do_split(int layout) {
	struct wmiiv_container *container = config->handler_context.container;
	struct wmiiv_workspace *ws = config->handler_context.workspace;
	if (container) {
		container_split(container, layout);
	} else {
		workspace_split(ws, layout);
	}

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		arrange_workspace(ws);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *do_unsplit() {
	// TODO (wmiiv)
	// struct wmiiv_container *container = config->handler_context.container;
	struct wmiiv_workspace *ws = config->handler_context.workspace;

	// TODO (wmiiv)
	// if (container && container->pending.parent && container->pending.parent->pending.children->length == 1) {
	// 	container_flatten(container->pending.parent);
	// } else {
	// 	return cmd_results_new(CMD_FAILURE, "Can only flatten a child container with no siblings");
	// }

	if (root->fullscreen_global) {
		arrange_root();
	} else {
		arrange_workspace(ws);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_split(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "split", EXPECTED_EQUAL_TO, 1))) {
		return error;
	}
	if (!root->outputs->length) {
		return cmd_results_new(CMD_INVALID,
				"Can't run this command while there's no outputs connected.");
	}
	if (strcasecmp(argv[0], "v") == 0 || strcasecmp(argv[0], "vertical") == 0) {
		return do_split(L_VERT);
	} else if (strcasecmp(argv[0], "h") == 0 ||
			strcasecmp(argv[0], "horizontal") == 0) {
		return do_split(L_HORIZ);
	} else if (strcasecmp(argv[0], "t") == 0 ||
			strcasecmp(argv[0], "toggle") == 0) {
		struct wmiiv_container *focused = config->handler_context.container;

		if (focused && container_parent_layout(focused) == L_VERT) {
			return do_split(L_HORIZ);
		} else {
			return do_split(L_VERT);
		}
	} else if (strcasecmp(argv[0], "n") == 0 ||
			strcasecmp(argv[0], "none") == 0) {
		return do_unsplit();
	} else {
		return cmd_results_new(CMD_FAILURE,
			"Invalid split command (expected either horizontal or vertical).");
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *cmd_splitv(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "splitv", EXPECTED_EQUAL_TO, 0))) {
		return error;
	}
	return do_split(L_VERT);
}

struct cmd_results *cmd_splith(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "splith", EXPECTED_EQUAL_TO, 0))) {
		return error;
	}
	return do_split(L_HORIZ);
}

struct cmd_results *cmd_splitt(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "splitt", EXPECTED_EQUAL_TO, 0))) {
		return error;
	}

	struct wmiiv_container *container = config->handler_context.container;

	if (container && container_parent_layout(container) == L_VERT) {
		return do_split(L_HORIZ);
	} else {
		return do_split(L_VERT);
	}
}
