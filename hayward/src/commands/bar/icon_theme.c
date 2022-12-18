#define _POSIX_C_SOURCE 200809L
#include <string.h>

#include "hayward-common/log.h"

#include "hayward/commands.h"
#include "hayward/config.h"

#include "config.h"

struct cmd_results *
bar_cmd_icon_theme(int argc, char **argv) {
#if HAVE_TRAY
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "icon_theme", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }

    hayward_log(
        HAYWARD_DEBUG, "[Bar %s] Setting icon theme to %s",
        config->current_bar->id, argv[0]
    );
    free(config->current_bar->icon_theme);
    config->current_bar->icon_theme = strdup(argv[0]);
    return cmd_results_new(CMD_SUCCESS, NULL);
#else
    return cmd_results_new(
        CMD_INVALID, "Hayward has been compiled without tray support"
    );
#endif
}
