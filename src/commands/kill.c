#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <hayward/config.h>
#include <hayward/profiler.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>

struct cmd_results *
cmd_kill(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct hwd_window *window = config->handler_context.window;
    if (window == NULL) {
        return cmd_results_new(CMD_INVALID, "Can only kill windows");
    }

    view_close(window->view);
    return cmd_results_new(CMD_SUCCESS, NULL);
}
