#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/server.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/view.h"
#include "list.h"
#include "log.h"

static void rebuild_textures_iterator(struct wmiiv_container *container, void *data) {
	container_update_title_textures(container);
	if (container_is_window(container)) {
		window_update_marks_textures(container);
	}
}

static void do_reload(void *data) {
	// store bar ids to check against new bars for barconfig_update events
	list_t *bar_ids = create_list();
	for (int i = 0; i < config->bars->length; ++i) {
		struct bar_config *bar = config->bars->items[i];
		list_add(bar_ids, strdup(bar->id));
	}

	const char *path = NULL;
	if (config->user_config_path) {
		path = config->current_config_path;
	}

	if (!load_main_config(path, true, false)) {
		wmiiv_log(WMIIV_ERROR, "Error(s) reloading config");
		list_free_items_and_destroy(bar_ids);
		return;
	}

	ipc_event_workspace(NULL, NULL, "reload");

	load_wmiivbars();

	for (int i = 0; i < config->bars->length; ++i) {
		struct bar_config *bar = config->bars->items[i];
		for (int j = 0; j < bar_ids->length; ++j) {
			if (strcmp(bar->id, bar_ids->items[j]) == 0) {
				ipc_event_barconfig_update(bar);
				break;
			}
		}
	}
	list_free_items_and_destroy(bar_ids);

	root_for_each_container(rebuild_textures_iterator, NULL);

	arrange_root();
}

struct cmd_results *cmd_reload(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "reload", EXPECTED_EQUAL_TO, 0))) {
		return error;
	}

	const char *path = NULL;
	if (config->user_config_path) {
		path = config->current_config_path;
	}

	if (!load_main_config(path, true, true)) {
		return cmd_results_new(CMD_FAILURE, "Error(s) reloading config.");
	}

	// The reload command frees a lot of stuff, so to avoid use-after-frees
	// we schedule the reload to happen using an idle event.
	wl_event_loop_add_idle(server.wl_event_loop, do_reload, NULL);

	return cmd_results_new(CMD_SUCCESS, NULL);
}