#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/list.h>
#include <hayward/stringop.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>

// Returns error object, or NULL if check succeeds.
struct cmd_results *
checkarg(int argc, const char *name, enum expected_args type, int val) {
    const char *error_name = NULL;
    switch (type) {
    case EXPECTED_AT_LEAST:
        if (argc < val) {
            error_name = "at least ";
        }
        break;
    case EXPECTED_AT_MOST:
        if (argc > val) {
            error_name = "at most ";
        }
        break;
    case EXPECTED_EQUAL_TO:
        if (argc != val) {
            error_name = "";
        }
    }
    return error_name ? cmd_results_new(
                            CMD_INVALID,
                            "Invalid %s command "
                            "(expected %s%d argument%s, got %d)",
                            name, error_name, val, val != 1 ? "s" : "", argc
                        )
                      : NULL;
}

/* Keep alphabetized */
static const struct cmd_handler handlers[] = {
    {"bindcode", cmd_bindcode},
    {"bindswitch", cmd_bindswitch},
    {"bindsym", cmd_bindsym},
    {"exec", cmd_exec},
    {"exec_always", cmd_exec_always},
    {"floating_maximum_size", cmd_floating_maximum_size},
    {"floating_minimum_size", cmd_floating_minimum_size},
    {"floating_modifier", cmd_floating_modifier},
    {"focus", cmd_focus},
    {"focus_follows_mouse", cmd_focus_follows_mouse},
    {"focus_on_window_activation", cmd_focus_on_window_activation},
    {"focus_wrapping", cmd_focus_wrapping},
    {"font", cmd_font},
    {"force_display_urgency_hint", cmd_force_display_urgency_hint},
    {"fullscreen", cmd_fullscreen},
    {"input", cmd_input},
    {"mode", cmd_mode},
    {"seat", cmd_seat},
    {"set", cmd_set},
    {"tiling_drag_threshold", cmd_tiling_drag_threshold},
    {"unbindcode", cmd_unbindcode},
    {"unbindswitch", cmd_unbindswitch},
    {"unbindsym", cmd_unbindsym},
    {"workspace", cmd_workspace},
};

/* Config-time only commands. Keep alphabetized */
static const struct cmd_handler config_handlers[] = {
    {"include", cmd_include},
    {"haywardnag_command", cmd_haywardnag_command},
    {"xwayland", cmd_xwayland},
};

/* Runtime-only commands. Keep alphabetized */
static const struct cmd_handler command_handlers[] = {
    {"exit", cmd_exit},             //
    {"floating", cmd_floating},     //
    {"fullscreen", cmd_fullscreen}, //
    {"kill", cmd_kill},             //
    {"layout", cmd_layout},         //
    {"move", cmd_move},             //
    {"nop", cmd_nop},               //
    {"reload", cmd_reload},
    {"resize", cmd_resize}, //
};

static int
handler_compare(const void *_a, const void *_b) {
    const struct cmd_handler *a = _a;
    const struct cmd_handler *b = _b;
    return strcasecmp(a->command, b->command);
}

const struct cmd_handler *
find_handler(char *line, const struct cmd_handler *handlers, size_t handlers_size) {
    if (!handlers || !handlers_size) {
        return NULL;
    }
    const struct cmd_handler query = {.command = line};
    return bsearch(
        &query, handlers, handlers_size / sizeof(struct cmd_handler), sizeof(struct cmd_handler),
        handler_compare
    );
}

static const struct cmd_handler *
find_handler_ex(
    char *line, const struct cmd_handler *config_handlers, size_t config_handlers_size,
    const struct cmd_handler *command_handlers, size_t command_handlers_size,
    const struct cmd_handler *handlers, size_t handlers_size
) {
    const struct cmd_handler *handler = NULL;
    if (config->reading) {
        handler = find_handler(line, config_handlers, config_handlers_size);
    } else if (config->active) {
        handler = find_handler(line, command_handlers, command_handlers_size);
    }
    return handler ? handler : find_handler(line, handlers, handlers_size);
}

static const struct cmd_handler *
find_core_handler(char *line) {
    return find_handler_ex(
        line, config_handlers, sizeof(config_handlers), command_handlers, sizeof(command_handlers),
        handlers, sizeof(handlers)
    );
}

list_t *
execute_command(char *_exec, struct hwd_seat *seat, struct hwd_window *window) {
    char *cmd;
    char matched_delim = ';';

    if (seat == NULL) {
        // passing a NULL seat means we just pick the default seat
        seat = input_manager_get_default_seat();
        assert(seat);
    }

    char *exec = strdup(_exec);
    char *head = exec;
    list_t *res_list = create_list();

    if (!res_list || !exec) {
        return NULL;
    }

    config->handler_context.seat = seat;

    do {
        for (; isspace(*head); ++head) {
        }

        // Split command list
        cmd = argsep(&head, ";,", &matched_delim);
        for (; isspace(*cmd); ++cmd) {
        }

        if (strcmp(cmd, "") == 0) {
            wlr_log(WLR_INFO, "Ignoring empty command.");
            continue;
        }
        wlr_log(WLR_INFO, "Handling command '%s'", cmd);
        // TODO better handling of argv
        int argc;
        char **argv = split_args(cmd, &argc);
        if (strcmp(argv[0], "exec") != 0 && strcmp(argv[0], "exec_always") != 0 &&
            strcmp(argv[0], "mode") != 0) {
            for (int i = 1; i < argc; ++i) {
                if (*argv[i] == '\"' || *argv[i] == '\'') {
                    strip_quotes(argv[i]);
                }
            }
        }
        const struct cmd_handler *handler = find_core_handler(argv[0]);
        if (!handler) {
            list_add(
                res_list, cmd_results_new(CMD_INVALID, "Unknown/invalid command '%s'", argv[0])
            );
            free_argv(argc, argv);
            goto cleanup;
        }

        // Var replacement, for all but first argument of set
        for (int i = handler->handle == cmd_set ? 2 : 1; i < argc; ++i) {
            argv[i] = do_var_replacement(argv[i]);
        }

        if (window == NULL) {
            window = root_get_focused_window(root);
        }

        if (window == NULL) {
            config->handler_context.workspace = root_get_active_workspace(root);
            config->handler_context.window = NULL;
        } else {
            config->handler_context.workspace = window->workspace;
            config->handler_context.window = window;
        }

        struct cmd_results *res = handler->handle(argc - 1, argv + 1);
        list_add(res_list, res);
        if (res->status == CMD_INVALID) {
            free_argv(argc, argv);
            goto cleanup;
        }
        free_argv(argc, argv);
    } while (head);
cleanup:
    free(exec);
    return res_list;
}

// this is like execute_command above, except:
// 1) it ignores empty commands (empty lines)
// 2) it does variable substitution
// 3) it doesn't split commands (because the multiple commands are supposed to
//	  be chained together)
// 4) execute_command handles all state internally while config_command has
// some state handled outside (notably the block mode, in read_config)
struct cmd_results *
config_command(char *exec, char **new_block) {
    struct cmd_results *results = NULL;
    int argc;
    char **argv = split_args(exec, &argc);

    // Check for empty lines
    if (!argc) {
        results = cmd_results_new(CMD_SUCCESS, NULL);
        goto cleanup;
    }

    // Check for the start of a block
    if (argc > 1 && strcmp(argv[argc - 1], "{") == 0) {
        *new_block = join_args(argv, argc - 1);
        results = cmd_results_new(CMD_BLOCK, NULL);
        goto cleanup;
    }

    // Check for the end of a block
    if (strcmp(argv[argc - 1], "}") == 0) {
        results = cmd_results_new(CMD_BLOCK_END, NULL);
        goto cleanup;
    }

    // Make sure the command is not stored in a variable
    if (*argv[0] == '$') {
        argv[0] = do_var_replacement(argv[0]);
        char *temp = join_args(argv, argc);
        free_argv(argc, argv);
        argv = split_args(temp, &argc);
        free(temp);
        if (!argc) {
            results = cmd_results_new(CMD_SUCCESS, NULL);
            goto cleanup;
        }
    }

    // Determine the command handler
    wlr_log(WLR_INFO, "Config command: %s", exec);
    const struct cmd_handler *handler = find_core_handler(argv[0]);
    if (!handler || !handler->handle) {
        const char *error =
            handler ? "Command '%s' is shimmed, but unimplemented" : "Unknown/invalid command '%s'";
        results = cmd_results_new(CMD_INVALID, error, argv[0]);
        goto cleanup;
    }

    // Do variable replacement
    if (handler->handle == cmd_set && argc > 1 && *argv[1] == '$') {
        // Escape the variable name so it does not get replaced by one shorter
        char *temp = calloc(1, strlen(argv[1]) + 2);
        temp[0] = '$';
        strcpy(&temp[1], argv[1]);
        free(argv[1]);
        argv[1] = temp;
    }
    char *command = do_var_replacement(join_args(argv, argc));
    wlr_log(WLR_INFO, "After replacement: %s", command);
    free_argv(argc, argv);
    argv = split_args(command, &argc);
    free(command);

    // Strip quotes and unescape the string
    for (int i = handler->handle == cmd_set ? 2 : 1; i < argc; ++i) {
        if (handler->handle != cmd_exec && handler->handle != cmd_exec_always &&
            handler->handle != cmd_mode && handler->handle != cmd_bindsym &&
            handler->handle != cmd_bindcode && handler->handle != cmd_bindswitch &&
            handler->handle != cmd_set && (*argv[i] == '\"' || *argv[i] == '\'')) {
            strip_quotes(argv[i]);
        }
        unescape_string(argv[i]);
    }

    // Run command
    results = handler->handle(argc - 1, argv + 1);

cleanup:
    free_argv(argc, argv);
    return results;
}

struct cmd_results *
config_subcommand(char **argv, int argc, const struct cmd_handler *handlers, size_t handlers_size) {
    char *command = join_args(argv, argc);
    wlr_log(WLR_DEBUG, "Subcommand: %s", command);
    free(command);

    const struct cmd_handler *handler = find_handler(argv[0], handlers, handlers_size);
    if (!handler) {
        return cmd_results_new(CMD_INVALID, "Unknown/invalid command '%s'", argv[0]);
    }
    if (handler->handle) {
        return handler->handle(argc - 1, argv + 1);
    }
    return cmd_results_new(CMD_INVALID, "The command '%s' is shimmed, but unimplemented", argv[0]);
}

struct cmd_results *
config_commands_command(char *exec) {
    struct cmd_results *results = NULL;
    int argc;
    char **argv = split_args(exec, &argc);
    if (!argc) {
        results = cmd_results_new(CMD_SUCCESS, NULL);
        goto cleanup;
    }

    // Find handler for the command this is setting a policy for
    char *cmd = argv[0];

    if (strcmp(cmd, "}") == 0) {
        results = cmd_results_new(CMD_BLOCK_END, NULL);
        goto cleanup;
    }

    const struct cmd_handler *handler = find_handler(cmd, NULL, 0);
    if (!handler && strcmp(cmd, "*") != 0) {
        results = cmd_results_new(CMD_INVALID, "Unknown/invalid command '%s'", cmd);
        goto cleanup;
    }

    results = cmd_results_new(CMD_SUCCESS, NULL);

cleanup:
    free_argv(argc, argv);
    return results;
}

struct cmd_results *
cmd_results_new(enum cmd_status status, const char *format, ...) {
    struct cmd_results *results = malloc(sizeof(struct cmd_results));
    if (!results) {
        wlr_log(WLR_ERROR, "Unable to allocate command results");
        return NULL;
    }
    results->status = status;
    if (format) {
        char *error = malloc(256);
        va_list args;
        va_start(args, format);
        if (error) {
            vsnprintf(error, 256, format, args);
        }
        va_end(args);
        results->error = error;
    } else {
        results->error = NULL;
    }
    return results;
}

void
free_cmd_results(struct cmd_results *results) {
    if (results->error) {
        free(results->error);
    }
    free(results);
}
