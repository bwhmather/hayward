#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdbool.h>
#include <string.h>

#include <hayward-common/util.h>

#include <hayward/config.h>

struct cmd_results *
cmd_xwayland(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "xwayland", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

#ifdef HAVE_XWAYLAND
    enum xwayland_mode xwayland;
    if (strcmp(argv[0], "force") == 0) {
        xwayland = XWAYLAND_MODE_IMMEDIATE;
    } else if (parse_boolean(argv[0], true)) {
        xwayland = XWAYLAND_MODE_LAZY;
    } else {
        xwayland = XWAYLAND_MODE_DISABLED;
    }

    if (config->reloading && config->xwayland != xwayland) {
        return cmd_results_new(CMD_FAILURE, "xwayland can only be enabled/disabled at launch");
    }
    config->xwayland = xwayland;
#else
    hwd_log(
        HWD_INFO,
        "Ignoring `xwayland` command, "
        "hayward hasn't been built with Xwayland support"
    );
#endif

    return cmd_results_new(CMD_SUCCESS, NULL);
}
