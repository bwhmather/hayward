#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>

#include <wayland-server-core.h>

#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/profiler.h>
#include <hayward/server.h>
#include <hayward/tree/root.h>
static void
do_reload(void *data) {
    const char *path = NULL;
    if (config->user_config_path) {
        path = config->current_config_path;
    }

    if (!load_main_config(path, true, false)) {
        wlr_log(WLR_ERROR, "Error(s) reloading config");
        return;
    }

    root_set_dirty(root);
}

struct cmd_results *
cmd_reload(int argc, char **argv) {
    HWD_PROFILER_TRACE();

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
