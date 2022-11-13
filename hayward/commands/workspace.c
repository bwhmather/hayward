#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include "hayward/commands.h"
#include "hayward/config.h"
#include "hayward/input/seat.h"
#include "hayward/tree/workspace.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

static struct workspace_config *workspace_config_find_or_create(char *workspace_name) {
	struct workspace_config *wsc = workspace_find_config(workspace_name);
	if (wsc) {
		return wsc;
	}
	wsc = calloc(1, sizeof(struct workspace_config));
	if (!wsc) {
		return NULL;
	}
	wsc->workspace = strdup(workspace_name);
	wsc->outputs = create_list();
	wsc->gaps_inner = INT_MIN;
	wsc->gaps_outer.top = INT_MIN;
	wsc->gaps_outer.right = INT_MIN;
	wsc->gaps_outer.bottom = INT_MIN;
	wsc->gaps_outer.left = INT_MIN;
	list_add(config->workspace_configs, wsc);
	return wsc;
}

void free_workspace_config(struct workspace_config *wsc) {
	free(wsc->workspace);
	list_free_items_and_destroy(wsc->outputs);
	free(wsc);
}

static void prevent_invalid_outer_gaps(struct workspace_config *wsc) {
	if (wsc->gaps_outer.top != INT_MIN &&
			wsc->gaps_outer.top < -wsc->gaps_inner) {
		wsc->gaps_outer.top = -wsc->gaps_inner;
	}
	if (wsc->gaps_outer.right != INT_MIN &&
			wsc->gaps_outer.right < -wsc->gaps_inner) {
		wsc->gaps_outer.right = -wsc->gaps_inner;
	}
	if (wsc->gaps_outer.bottom != INT_MIN &&
			wsc->gaps_outer.bottom < -wsc->gaps_inner) {
		wsc->gaps_outer.bottom = -wsc->gaps_inner;
	}
	if (wsc->gaps_outer.left != INT_MIN &&
			wsc->gaps_outer.left < -wsc->gaps_inner) {
		wsc->gaps_outer.left = -wsc->gaps_inner;
	}
}

static struct cmd_results *cmd_workspace_gaps(int argc, char **argv,
		int gaps_location) {
	const char expected[] = "Expected 'workspace <name> gaps "
		"inner|outer|horizontal|vertical|top|right|bottom|left <px>'";
	if (gaps_location == 0) {
		return cmd_results_new(CMD_INVALID, expected);
	}
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "workspace", EXPECTED_EQUAL_TO,
					gaps_location + 3))) {
		return error;
	}
	char *workspace_name = join_args(argv, argc - 3);
	struct workspace_config *wsc = workspace_config_find_or_create(workspace_name);
	free(workspace_name);
	if (!wsc) {
		return cmd_results_new(CMD_FAILURE,
				"Unable to allocate workspace output");
	}

	char *end;
	int amount = strtol(argv[gaps_location + 2], &end, 10);
	if (strlen(end)) {
		return cmd_results_new(CMD_FAILURE, expected);
	}

	bool valid = false;
	char *type = argv[gaps_location + 1];
	if (!strcasecmp(type, "inner")) {
		valid = true;
		wsc->gaps_inner = (amount >= 0) ? amount : 0;
	} else {
		if (!strcasecmp(type, "outer") || !strcasecmp(type, "vertical")
				|| !strcasecmp(type, "top")) {
			valid = true;
			wsc->gaps_outer.top = amount;
		}
		if (!strcasecmp(type, "outer") || !strcasecmp(type, "horizontal")
				|| !strcasecmp(type, "right")) {
			valid = true;
			wsc->gaps_outer.right = amount;
		}
		if (!strcasecmp(type, "outer") || !strcasecmp(type, "vertical")
				|| !strcasecmp(type, "bottom")) {
			valid = true;
			wsc->gaps_outer.bottom = amount;
		}
		if (!strcasecmp(type, "outer") || !strcasecmp(type, "horizontal")
				|| !strcasecmp(type, "left")) {
			valid = true;
			wsc->gaps_outer.left = amount;
		}
	}
	if (!valid) {
		return cmd_results_new(CMD_INVALID, expected);
	}

	// Prevent invalid gaps configurations.
	if (wsc->gaps_inner != INT_MIN && wsc->gaps_inner < 0) {
		wsc->gaps_inner = 0;
	}
	prevent_invalid_outer_gaps(wsc);

	return error;
}

struct cmd_results *cmd_workspace(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "workspace", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	int gaps_location = -1;

	for (int i = 0; i < argc; ++i) {
		if (strcasecmp(argv[i], "gaps") == 0) {
			gaps_location = i;
			break;
		}
	}
	if (gaps_location >= 0) {
		if ((error = cmd_workspace_gaps(argc, argv, gaps_location))) {
			return error;
		}
	} else {
		if (config->reading || !config->active) {
			return cmd_results_new(CMD_DEFER, NULL);
		} else if (!root->outputs->length) {
			return cmd_results_new(CMD_INVALID,
					"Can't run this command while there's no outputs connected.");
		}

		struct hayward_workspace *workspace = NULL;
		if (strcasecmp(argv[0], "number") == 0) {
			if (argc != 2) {
				return cmd_results_new(CMD_INVALID,
						"Expected workspace number");
			}
			if (!isdigit(argv[1][0])) {
				return cmd_results_new(CMD_INVALID,
						"Invalid workspace number '%s'", argv[1]);
			}
			if (!(workspace = workspace_by_name(argv[1]))) {
				workspace = workspace_create(argv[1]);
			}
		} else {
			char *name = join_args(argv, argc);
			if (!(workspace = workspace_by_name(name))) {
				workspace = workspace_create(name);
			}
			free(name);
		}
		if (!workspace) {
			return cmd_results_new(CMD_FAILURE, "No workspace to switch to");
		}
		root_set_active_workspace(workspace);
	}
	return cmd_results_new(CMD_SUCCESS, NULL);
}
