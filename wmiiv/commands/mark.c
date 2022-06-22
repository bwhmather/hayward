#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/tree/view.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

// mark foo                      Same as mark --replace foo
// mark --add foo                Add this mark to view's list
// mark --replace foo            Replace view's marks with this single one
// mark --add --toggle foo       Toggle current mark and persist other marks
// mark --replace --toggle foo   Toggle current mark and remove other marks

struct cmd_results *cmd_mark(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "mark", EXPECTED_AT_LEAST, 1))) {
		return error;
	}
	struct wmiiv_window *window = config->handler_context.window;
	if (!window) {
		return cmd_results_new(CMD_INVALID, "Only windows can have marks");
	}

	bool add = false, toggle = false;
	while (argc > 0 && strncmp(*argv, "--", 2) == 0) {
		if (strcmp(*argv, "--add") == 0) {
			add = true;
		} else if (strcmp(*argv, "--replace") == 0) {
			add = false;
		} else if (strcmp(*argv, "--toggle") == 0) {
			toggle = true;
		} else {
			return cmd_results_new(CMD_INVALID,
					"Unrecognized argument '%s'", *argv);
		}
		++argv;
		--argc;
	}

	if (!argc) {
		return cmd_results_new(CMD_INVALID,
				"Expected '[--add|--replace] [--toggle] <identifier>'");
	}

	char *mark = join_args(argv, argc);
	bool had_mark = window_has_mark(window, mark);

	if (!add) {
		// Replacing
		window_clear_marks(window);
	}

	window_find_and_unmark(mark);

	if (!toggle || !had_mark) {
		window_add_mark(window, mark);
	}

	free(mark);
	window_update_marks_textures(window);
	view_execute_criteria(window->view);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
