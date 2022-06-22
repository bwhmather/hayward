#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/tree/root.h"
#include "wmiiv/tree/view.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

static void remove_all_marks_iterator(struct wmiiv_container *container, void *data) {
	if (container_is_window(container)) {
		window_clear_marks(container);
		window_update_marks_textures(container);
	}
}

// unmark                  Remove all marks from all views
// unmark foo              Remove single mark from whichever view has it
// [criteria] unmark       Remove all marks from matched view
// [criteria] unmark foo   Remove single mark from matched view

struct cmd_results *cmd_unmark(int argc, char **argv) {
	// Determine the window
	struct wmiiv_container *window = NULL;
	if (config->handler_context.node_overridden) {
		window = config->handler_context.window;
	}

	if (window) {
		return cmd_results_new(CMD_INVALID, "Only windows can have marks");
	}

	// Determine the mark
	char *mark = NULL;
	if (argc > 0) {
		mark = join_args(argv, argc);
	}

	if (window && mark) {
		// Remove the mark from the given window
		if (window_has_mark(window, mark)) {
			window_find_and_unmark(mark);
		}
	} else if (window && !mark) {
		// Clear all marks from the given window
		window_clear_marks(window);
		window_update_marks_textures(window);
	} else if (!window && mark) {
		// Remove mark from whichever window has it
		window_find_and_unmark(mark);
	} else {
		// Remove all marks from all windows
		root_for_each_window(remove_all_marks_iterator, NULL);
	}
	free(mark);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
