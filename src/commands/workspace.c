#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/stringop.h>
#include <hayward/tree/root.h>
#include <hayward/tree/workspace.h>

struct cmd_results *
cmd_workspace(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "workspace", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    if (config->reading || !config->active) {
        return cmd_results_new(CMD_DEFER, NULL);
    } else if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID, "Can't run this command while there's no outputs connected."
        );
    }

    struct hwd_workspace *workspace = NULL;
    if (strcasecmp(argv[0], "number") == 0) {
        if (argc != 2) {
            return cmd_results_new(CMD_INVALID, "Expected workspace number");
        }
        if (!isdigit(argv[1][0])) {
            return cmd_results_new(CMD_INVALID, "Invalid workspace number '%s'", argv[1]);
        }
        if (!(workspace = workspace_by_name(argv[1]))) {
            workspace = workspace_create(argv[1]);
            root_add_workspace(root, workspace);
        }
    } else {
        char *name = join_args(argv, argc);
        if (!(workspace = workspace_by_name(name))) {
            workspace = workspace_create(name);
            root_add_workspace(root, workspace);
        }
        free(name);
    }
    if (!workspace) {
        return cmd_results_new(CMD_FAILURE, "No workspace to switch to");
    }
    root_set_active_workspace(root, workspace);
    root_arrange(root);
    root_commit_focus(root);
    return cmd_results_new(CMD_SUCCESS, NULL);
}
