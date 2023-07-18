#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/commands.h"

#include <stdbool.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/config.h>

#include <config.h>
#if WLR_HAS_X11_BACKEND
#include <wlr/backend/x11.h>
#endif
#include "hayward/commands.h"

#include <hayward-common/log.h>

#include <hayward/server.h>

static void
create_output(struct wlr_backend *backend, void *data) {
    bool *done = data;
    if (*done) {
        return;
    }

    if (wlr_backend_is_wl(backend)) {
        wlr_wl_output_create(backend);
        *done = true;
    } else if (wlr_backend_is_headless(backend)) {
        wlr_headless_add_output(backend, 1920, 1080);
        *done = true;
    }
#if WLR_HAS_X11_BACKEND
    else if (wlr_backend_is_x11(backend)) {
        wlr_x11_output_create(backend);
        *done = true;
    }
#endif
}

/**
 * This command is intended for developer use only.
 */
struct cmd_results *
cmd_create_output(int argc, char **argv) {
    hwd_assert(wlr_backend_is_multi(server.backend), "Expected a multi backend");

    bool done = false;
    wlr_multi_for_each_backend(server.backend, create_output, &done);

    if (!done) {
        return cmd_results_new(
            CMD_INVALID, "Can only create outputs for Wayland, X11 or headless backends"
        );
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
