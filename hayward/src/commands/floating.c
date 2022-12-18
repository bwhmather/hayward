#include <string.h>
#include <strings.h>

#include "hayward-common/list.h"
#include "hayward-common/util.h"

#include "hayward/commands.h"
#include "hayward/input/seat.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/view.h"
#include "hayward/tree/window.h"
#include "hayward/tree/workspace.h"

struct cmd_results *
cmd_floating(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "floating", EXPECTED_EQUAL_TO, 1))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID,
            "Can't run this command while there's no outputs connected."
        );
    }
    struct hayward_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_INVALID, "Can only float windows");
    }

    bool wants_floating = parse_boolean(argv[0], window_is_floating(window));

    window_set_floating(window, wants_floating);

    arrange_workspace(window->pending.workspace);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
