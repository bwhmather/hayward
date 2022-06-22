#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/tree/root.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/output.h"
#include "list.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

static void rebuild_marks_iterator(struct wmiiv_container *container, void *data) {
	if (container_is_window(container)) {
		window_update_marks_textures(container);
	}
}

struct cmd_results *cmd_show_marks(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "show_marks", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	config->show_marks = parse_boolean(argv[0], config->show_marks);

	if (config->show_marks) {
		root_for_each_window(rebuild_marks_iterator, NULL);
	}

	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_whole(output);
	}

	return cmd_results_new(CMD_SUCCESS, NULL);
}
