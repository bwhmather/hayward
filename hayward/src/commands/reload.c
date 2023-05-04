#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/commands.h"

#include <stdbool.h>
#include <string.h>
#include <wayland-server-core.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/ipc-server.h>
#include <hayward/server.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>

#include <config.h>

static void
do_reload(void *data) {
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
        hayward_log(HAYWARD_ERROR, "Error(s) reloading config");
        list_free_items_and_destroy(bar_ids);
        return;
    }

    ipc_event_workspace(NULL, NULL, "reload");

    load_haywardbars();

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

    arrange_root(root);
}

struct cmd_results *
cmd_reload(int argc, char **argv) {
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
