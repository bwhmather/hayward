#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <assert.h>
#include <float.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_layout.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/input/seat.h>
#include <hayward/list.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

static bool
parse_direction(const char *name, enum wlr_direction *out) {
    if (strcasecmp(name, "left") == 0) {
        *out = WLR_DIRECTION_LEFT;
    } else if (strcasecmp(name, "right") == 0) {
        *out = WLR_DIRECTION_RIGHT;
    } else if (strcasecmp(name, "up") == 0) {
        *out = WLR_DIRECTION_UP;
    } else if (strcasecmp(name, "down") == 0) {
        *out = WLR_DIRECTION_DOWN;
    } else {
        return false;
    }

    return true;
}

/**
 * Returns the node that should be focused if entering an output by moving
 * in the given direction.
 *
 *  Node should always be either a workspace or a window.
 */
static struct hwd_window *
get_window_in_output_direction(struct hwd_output *output, enum wlr_direction dir) {
    assert(output != NULL);

    struct hwd_workspace *workspace = root_get_active_workspace(root);
    assert(workspace != NULL);

    if (output->pending.fullscreen_window) {
        return output->pending.fullscreen_window;
    }

    switch (dir) {
    case WLR_DIRECTION_LEFT: {
        struct hwd_column *column = workspace_get_column_last(workspace, output);
        if (column == NULL) {
            return NULL;
        }
        return column->pending.active_child;
    }
    case WLR_DIRECTION_RIGHT: {
        struct hwd_column *column = workspace_get_column_first(workspace, output);
        if (column == NULL) {
            return NULL;
        }
        return column->pending.active_child;
    }
    case WLR_DIRECTION_UP: {
        return workspace_get_active_tiling_window(workspace);
    }
    case WLR_DIRECTION_DOWN: {
        return workspace_get_active_tiling_window(workspace);
    }
    }
    return NULL;
}

static struct hwd_window *
window_get_in_direction_tiling(
    struct hwd_window *window, struct hwd_seat *seat, enum wlr_direction dir
) {
    struct hwd_window *wrap_candidate = NULL;

    struct hwd_workspace *workspace = window->pending.workspace;
    struct hwd_output *output = window_get_output(window);

    if (window->pending.fullscreen) {
        // Fullscreen container with a direction - go straight to outputs
        struct hwd_output *new_output = output_get_in_direction(output, dir);
        if (!new_output) {
            return NULL;
        }
        return get_window_in_output_direction(new_output, dir);
    }

    switch (dir) {
    case WLR_DIRECTION_UP: {
        struct hwd_column *column = window->pending.parent;

        struct hwd_window *prev_sibling = window_get_previous_sibling(window);
        if (prev_sibling != NULL) {
            return prev_sibling;
        }

        if (config->focus_wrapping != WRAP_NO && !wrap_candidate) {
            prev_sibling = column_get_last_child(column);
            if (prev_sibling == window) {
                break;
            }
            wrap_candidate = prev_sibling;
            if (config->focus_wrapping == WRAP_FORCE) {
                return wrap_candidate;
            }
        }
        break;
    }
    case WLR_DIRECTION_DOWN: {
        struct hwd_column *column = window->pending.parent;

        struct hwd_window *next_sibling = window_get_next_sibling(window);
        if (next_sibling != NULL) {
            return next_sibling;
        }

        if (config->focus_wrapping != WRAP_NO && !wrap_candidate) {
            next_sibling = column_get_first_child(column);
            if (next_sibling == window) {
                break;
            }
            wrap_candidate = next_sibling;
            if (config->focus_wrapping == WRAP_FORCE) {
                return wrap_candidate;
            }
        }
        break;
    }
    case WLR_DIRECTION_LEFT: {
        struct hwd_column *column = window->pending.parent;

        struct hwd_column *next_column = workspace_get_column_before(workspace, column);
        if (next_column != NULL) {
            return next_column->pending.active_child;
        }

        if (config->focus_wrapping == WRAP_NO) {
            break;
        }

        struct hwd_column *wrap_column = workspace_get_column_last(workspace, output);
        wrap_candidate = wrap_column->pending.active_child;
        if (config->focus_wrapping == WRAP_FORCE && wrap_candidate != NULL) {
            return wrap_candidate;
        }
        break;
    }
    case WLR_DIRECTION_RIGHT: {
        struct hwd_column *column = window->pending.parent;

        struct hwd_column *next_column = workspace_get_column_after(workspace, column);
        if (next_column != NULL) {
            return next_column->pending.active_child;
        }

        if (config->focus_wrapping == WRAP_NO) {
            break;
        }

        struct hwd_column *wrap_column = workspace_get_column_first(workspace, output);
        wrap_candidate = wrap_column->pending.active_child;
        if (config->focus_wrapping == WRAP_FORCE && wrap_candidate != NULL) {
            return wrap_candidate;
        }
        break;
    }
    }

    // Check a different output
    struct hwd_output *new_output = output_get_in_direction(output, dir);
    if (config->focus_wrapping != WRAP_WORKSPACE && new_output) {
        return get_window_in_output_direction(new_output, dir);
    }

    // If there is a wrap candidate, return its focus inactive view
    if (wrap_candidate) {
        return wrap_candidate;
    }

    return NULL;
}

static struct hwd_window *
window_get_in_direction_floating(
    struct hwd_window *container, struct hwd_seat *seat, enum wlr_direction dir
) {
    double ref_lx = container->pending.x + container->pending.width / 2;
    double ref_ly = container->pending.y + container->pending.height / 2;
    double closest_distance = DBL_MAX;
    struct hwd_window *closest_container = NULL;

    if (!container->pending.workspace) {
        return NULL;
    }

    for (int i = 0; i < container->pending.workspace->pending.floating->length; i++) {
        struct hwd_window *floater = container->pending.workspace->pending.floating->items[i];
        if (floater == container) {
            continue;
        }
        float distance = dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_RIGHT
            ? (floater->pending.x + floater->pending.width / 2) - ref_lx
            : (floater->pending.y + floater->pending.height / 2) - ref_ly;
        if (dir == WLR_DIRECTION_LEFT || dir == WLR_DIRECTION_UP) {
            distance = -distance;
        }
        if (distance < 0) {
            continue;
        }
        if (distance < closest_distance) {
            closest_distance = distance;
            closest_container = floater;
        }
    }

    return closest_container;
}

static struct cmd_results *
focus_mode(struct hwd_workspace *workspace, bool floating) {
    struct hwd_window *new_focus = NULL;
    if (floating) {
        new_focus = workspace_get_active_floating_window(workspace);
    } else {
        new_focus = workspace_get_active_tiling_window(workspace);
    }
    if (new_focus) {
        workspace_set_active_window(workspace, new_focus);
        root_commit_focus(root);
    } else {
        return cmd_results_new(
            CMD_FAILURE, "Failed to find a %s container in workspace.",
            floating ? "floating" : "tiling"
        );
    }
    return cmd_results_new(CMD_SUCCESS, NULL);
}

struct cmd_results *
cmd_focus(int argc, char **argv) {
    if (config->reading || !config->active) {
        return cmd_results_new(CMD_DEFER, NULL);
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID, "Can't run this command while there are no outputs connected."
        );
    }
    struct hwd_workspace *workspace = config->handler_context.workspace;
    struct hwd_seat *seat = config->handler_context.seat;

    struct hwd_output *output = root_get_active_output(root);
    assert(output != NULL);

    struct hwd_window *window = root_get_focused_window(root);

    if (argc == 0) {
        return cmd_results_new(
            CMD_INVALID, "Expected 'focus <direction|mode_toggle|floating|tiling>' "
        );
    }

    if (strcmp(argv[0], "floating") == 0) {
        return focus_mode(workspace, true);
    } else if (strcmp(argv[0], "tiling") == 0) {
        return focus_mode(workspace, false);
    } else if (strcmp(argv[0], "mode_toggle") == 0) {
        bool floating = window && window_is_floating(window);
        return focus_mode(workspace, !floating);
    }

    enum wlr_direction direction = 0;
    if (!parse_direction(argv[0], &direction)) {
        return cmd_results_new(
            CMD_INVALID, "Expected 'focus <direction|mode_toggle|floating|tiling>' "
        );
    }

    if (!direction) {
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    if (window == NULL) {
        // Jump to the next output
        struct hwd_output *new_output = output_get_in_direction(output, direction);
        if (!new_output) {
            return cmd_results_new(CMD_SUCCESS, NULL);
        }

        window = get_window_in_output_direction(new_output, direction);
        if (window != NULL) {
            root_set_focused_window(root, window);
        } else {
            workspace_set_active_window(workspace, NULL);
            root_set_active_output(root, new_output);
        }
        root_commit_focus(root);
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    struct hwd_window *next_focus = NULL;
    if (window_is_floating(window) && !window->pending.fullscreen) {
        next_focus = window_get_in_direction_floating(window, seat, direction);
    } else {
        next_focus = window_get_in_direction_tiling(window, seat, direction);
    }
    if (next_focus) {
        root_set_focused_window(root, next_focus);
        root_commit_focus(root);
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}
