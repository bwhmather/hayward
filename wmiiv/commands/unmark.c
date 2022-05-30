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
	// Determine the container
	struct wmiiv_container *container = NULL;
	if (config->handler_context.node_overridden) {
		container = config->handler_context.container;
	}

	if (container && !container_is_window(container)) {
		return cmd_results_new(CMD_INVALID, "Only windows can have marks");
	}

	// Determine the mark
	char *mark = NULL;
	if (argc > 0) {
		mark = join_args(argv, argc);
	}

	if (container && mark) {
		// Remove the mark from the given container
		if (window_has_mark(container, mark)) {
			window_find_and_unmark(mark);
		}
	} else if (container && !mark) {
		// Clear all marks from the given container
		window_clear_marks(container);
		window_update_marks_textures(container);
	} else if (!container && mark) {
		// Remove mark from whichever container has it
		window_find_and_unmark(mark);
	} else {
		// Remove all marks from all containers
		root_for_each_container(remove_all_marks_iterator, NULL);
	}
	free(mark);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
