#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/commands.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/seat.h>
#include <hayward/list.h>
#include <hayward/log.h>
#include <hayward/stringop.h>
#include <hayward/tree.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>
#include <hayward/util.h>

static const char expected_syntax[] = "Expected 'move <left|right|up|down> <[px] px>' or "
                                      "'move <window> [to] workspace <name>' or "
                                      "'move <window|workspace> [to] output <name|direction>'";

static enum wlr_direction
opposite_direction(enum wlr_direction d) {
    switch (d) {
    case WLR_DIRECTION_UP:
        return WLR_DIRECTION_DOWN;
    case WLR_DIRECTION_DOWN:
        return WLR_DIRECTION_UP;
    case WLR_DIRECTION_RIGHT:
        return WLR_DIRECTION_LEFT;
    case WLR_DIRECTION_LEFT:
        return WLR_DIRECTION_RIGHT;
    }
    hwd_assert(false, "Unknown direction");
    return 0;
}

static struct hwd_output *
output_in_direction(
    const char *direction_string, struct hwd_output *reference, int ref_lx, int ref_ly
) {

    struct {
        char *name;
        enum wlr_direction direction;
    } names[] = {
        {"up", WLR_DIRECTION_UP},
        {"down", WLR_DIRECTION_DOWN},
        {"left", WLR_DIRECTION_LEFT},
        {"right", WLR_DIRECTION_RIGHT},
    };

    enum wlr_direction direction = 0;

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcasecmp(names[i].name, direction_string) == 0) {
            direction = names[i].direction;
            break;
        }
    }

    if (reference && direction) {
        struct wlr_output *target = wlr_output_layout_adjacent_output(
            root->output_layout, direction, reference->wlr_output, ref_lx, ref_ly
        );

        if (!target) {
            target = wlr_output_layout_farthest_output(
                root->output_layout, opposite_direction(direction), reference->wlr_output, ref_lx,
                ref_ly
            );
        }

        if (target) {
            return target->data;
        }
    }

    return output_by_name_or_id(direction_string);
}

static bool
window_move_to_next_output(
    struct hwd_window *window, struct hwd_output *output, enum wlr_direction move_dir
) {
    struct hwd_output *next_output = output_get_in_direction(output, move_dir);
    if (!next_output) {
        return false;
    }
    hwd_move_window_to_output_from_direction(window, output, move_dir);
    return true;
}

// Returns true if moved
static bool
window_move_in_direction(struct hwd_window *window, enum wlr_direction move_dir) {
    // If moving a fullscreen view, only consider outputs
    if (window->pending.fullscreen) {
        return window_move_to_next_output(window, window->pending.parent->pending.output, move_dir);
    }

    if (window_is_floating(window)) {
        return false;
    }

    // TODO (hayward) windows should always have a parent if not floating.
    if (!window->pending.parent) {
        return false;
    }

    struct hwd_column *old_column = window->pending.parent;
    struct hwd_workspace *workspace = old_column->pending.workspace;

    switch (move_dir) {
    case WLR_DIRECTION_UP: {
        struct hwd_window *prev_sibling = window_get_previous_sibling(window);
        if (prev_sibling == NULL) {
            return window_move_to_next_output(window, window->pending.output, move_dir);
        }

        window_detach(window);
        column_add_sibling(prev_sibling, window, false);
        return true;
    }
    case WLR_DIRECTION_DOWN: {
        struct hwd_window *next_sibling = window_get_next_sibling(window);
        if (next_sibling == NULL) {
            return window_move_to_next_output(window, window->pending.output, move_dir);
        }

        window_detach(window);
        column_add_sibling(next_sibling, window, true);
        return true;
    }
    case WLR_DIRECTION_LEFT: {
        struct hwd_column *new_column = workspace_get_column_before(workspace, old_column);

        if (new_column == NULL) {
            // Window is already in the left most column.   If window is the
            // only child of this column then attempt to move it to the next
            // workspace, otherwise insert a new column to  the left and carry
            // on as before.
            if (old_column->pending.children->length == 1) {
                return window_move_to_next_output(window, old_column->pending.output, move_dir);
            }

            new_column = column_create();
            new_column->pending.height = new_column->pending.width = 0;
            new_column->width_fraction = 0;
            new_column->pending.layout = L_STACKED;

            workspace_insert_column_first(workspace, old_column->pending.output, new_column);
        }

        hwd_move_window_to_column_from_direction(window, new_column, move_dir);

        return true;
    }
    case WLR_DIRECTION_RIGHT: {
        struct hwd_column *new_column = workspace_get_column_after(workspace, old_column);

        if (new_column == NULL) {
            // Window is already in the right most column.  If window is the
            // only child of this column then attempt to move it to the next
            // workspace, otherwise insert a new column to the right and carry
            // on as before.
            if (old_column->pending.children->length == 1) {
                return window_move_to_next_output(window, old_column->pending.output, move_dir);
            }

            new_column = column_create();
            new_column->pending.height = new_column->pending.width = 0;
            new_column->width_fraction = 0;
            new_column->pending.layout = L_STACKED;

            workspace_insert_column_last(workspace, old_column->pending.output, new_column);
        }

        hwd_move_window_to_column_from_direction(window, new_column, move_dir);

        return true;
    }
    }
    return false; // TODO unreachable.
}

static struct cmd_results *
cmd_move_window(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "move window", EXPECTED_AT_LEAST, 2))) {
        return error;
    }

    struct hwd_window *window = config->handler_context.window;

    if (!window) {
        return cmd_results_new(CMD_FAILURE, "Can only move windows");
    }

    struct hwd_column *old_parent = window->pending.parent;
    struct hwd_workspace *old_workspace = window->pending.workspace;
    struct hwd_output *old_output = window_get_output(window);

    // save focus, in case it needs to be restored
    struct hwd_window *focus = root_get_focused_window(root);

    // determine destination
    if (strcasecmp(argv[0], "workspace") == 0) {
        // Determine which workspace the window should be moved to.
        struct hwd_workspace *workspace = NULL;
        char *workspace_name = NULL;
        if (strcasecmp(argv[1], "next") == 0 || strcasecmp(argv[1], "prev") == 0 ||
            strcasecmp(argv[1], "next_on_output") == 0 ||
            strcasecmp(argv[1], "prev_on_output") == 0) {
            workspace = workspace_by_name(argv[1]);
        } else {
            if (strcasecmp(argv[1], "number") == 0) {
                // move [window] [to] "workspace number x"
                if (argc != 3) {
                    return cmd_results_new(CMD_INVALID, expected_syntax);
                }
                if (!isdigit(argv[2][0])) {
                    return cmd_results_new(CMD_INVALID, "Invalid workspace number '%s'", argv[2]);
                }
                workspace_name = join_args(argv + 2, argc - 2);
                workspace = workspace_by_name(workspace_name);
            } else {
                workspace_name = join_args(argv + 1, argc - 1);
                workspace = workspace_by_name(workspace_name);
            }
        }
        if (!workspace) {
            workspace = workspace_create(workspace_name);
            root_add_workspace(root, workspace);
        }
        free(workspace_name);

        // Do the move.
        hwd_move_window_to_workspace(window, workspace);
        workspace_set_active_window(workspace, window);

        // If necessary, clean up old column and workspace.
        if (old_parent) {
            column_consider_destroy(old_parent);
        }
        if (old_workspace) {
            workspace_consider_destroy(old_workspace);
        }

        // Re-arrange windows
        if (old_workspace && !old_workspace->pending.dead) {
            workspace_arrange(old_workspace);
        }
        // TODO (hayward) it should often be possible to get away without
        // rearranging the entire workspace.
        workspace_arrange(workspace);

        root_commit_focus(root);

        return cmd_results_new(CMD_SUCCESS, NULL);

    } else if (strcasecmp(argv[0], "output") == 0) {
        struct hwd_output *new_output =
            output_in_direction(argv[1], old_output, window->pending.x, window->pending.y);
        if (!new_output) {
            return cmd_results_new(
                CMD_FAILURE, "Can't find output with name/direction '%s'", argv[1]
            );
        }

        hwd_move_window_to_output(window, new_output);

        if (focus == window) {
            focus = NULL;
            if (old_parent) {
                focus = old_parent->pending.active_child;
            }
            if (!focus && old_workspace) {
                focus = workspace_get_active_window(old_workspace);
            }
        }
        if (focus != NULL) {
            root_set_focused_window(root, focus);
        }
        root_commit_focus(root);

        if (old_parent) {
            column_consider_destroy(old_parent);
        }
        if (old_workspace) {
            workspace_consider_destroy(old_workspace);
        }

        output_arrange(old_output);
        output_arrange(new_output);

        return cmd_results_new(CMD_SUCCESS, NULL);
    } else {
        return cmd_results_new(CMD_INVALID, expected_syntax);
    }
}

static struct cmd_results *
cmd_move_in_direction(enum wlr_direction direction, int argc, char **argv) {
    int move_amt = 10;
    if (argc) {
        char *inv;
        move_amt = (int)strtol(argv[0], &inv, 10);
        if (*inv != '\0' && strcasecmp(inv, "px") != 0) {
            return cmd_results_new(CMD_FAILURE, "Invalid distance specified");
        }
    }

    struct hwd_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_FAILURE, "Cannot move workspaces in a direction");
    }
    if (window_is_floating(window)) {
        if (window->pending.fullscreen) {
            return cmd_results_new(CMD_FAILURE, "Cannot move fullscreen floating window");
        }
        double lx = window->pending.x;
        double ly = window->pending.y;
        switch (direction) {
        case WLR_DIRECTION_LEFT:
            lx -= move_amt;
            break;
        case WLR_DIRECTION_RIGHT:
            lx += move_amt;
            break;
        case WLR_DIRECTION_UP:
            ly -= move_amt;
            break;
        case WLR_DIRECTION_DOWN:
            ly += move_amt;
            break;
        }

        double cx = lx + window->pending.width / 2;
        double cy = ly + window->pending.height / 2;
        struct hwd_output *output = root_find_closest_output(root, cx, cy);

        window_floating_move_to(window, output, lx, ly);
        return cmd_results_new(CMD_SUCCESS, NULL);
    }
    struct hwd_workspace *old_workspace = window->pending.workspace;
    struct hwd_column *old_parent = window->pending.parent;

    if (!window_move_in_direction(window, direction)) {
        // Container didn't move
        return cmd_results_new(CMD_SUCCESS, NULL);
    }

    // clean-up, destroying parents if the window was the last child
    if (old_parent) {
        column_consider_destroy(old_parent);
    } else if (old_workspace) {
        // TODO (hayward) shouldn't be possible to hit this.
        workspace_consider_destroy(old_workspace);
    }

    struct hwd_workspace *new_workspace = window->pending.workspace;

    workspace_arrange(old_workspace);
    if (new_workspace != old_workspace) {
        workspace_arrange(new_workspace);
    }

    // Hack to re-focus window
    root_set_focused_window(root, window);

    if (old_workspace != new_workspace) {
        workspace_detect_urgent(old_workspace);
        workspace_detect_urgent(new_workspace);
    }
    window_end_mouse_operation(window);

    return cmd_results_new(CMD_SUCCESS, NULL);
}

static struct cmd_results *
cmd_move_to_position_pointer(struct hwd_window *window) {
    struct hwd_seat *seat = config->handler_context.seat;
    if (!seat->cursor) {
        return cmd_results_new(CMD_FAILURE, "No cursor device");
    }
    struct wlr_cursor *cursor = seat->cursor->cursor;
    /* Determine where to put the window. */
    double lx = cursor->x - window->pending.width / 2;
    double ly = cursor->y - window->pending.height / 2;

    /* Correct target coordinates to be in bounds (on screen). */
    struct wlr_output *output =
        wlr_output_layout_output_at(root->output_layout, cursor->x, cursor->y);
    if (output) {
        struct wlr_box box;
        wlr_output_layout_get_box(root->output_layout, output, &box);
        lx = fmax(lx, box.x);
        ly = fmax(ly, box.y);
        if (lx + window->pending.width > box.x + box.width) {
            lx = box.x + box.width - window->pending.width;
        }
        if (ly + window->pending.height > box.y + box.height) {
            ly = box.y + box.height - window->pending.height;
        }
    }

    /* Actually move the window. */
    window_floating_move_to(window, output_from_wlr_output(output), lx, ly);
    return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_position_syntax[] =
    "Expected 'move [absolute] position <x> [px] <y> [px]' or "
    "'move [absolute] position center' or "
    "'move position cursor|mouse|pointer'";

static struct cmd_results *
cmd_move_to_position(int argc, char **argv) {
    struct hwd_window *window = config->handler_context.window;
    if (!window || !window_is_floating(window)) {
        return cmd_results_new(
            CMD_FAILURE,
            "Only floating windows "
            "can be moved to an absolute position"
        );
    }

    if (!argc) {
        return cmd_results_new(CMD_INVALID, expected_position_syntax);
    }

    bool absolute = false;
    if (strcmp(argv[0], "absolute") == 0) {
        absolute = true;
        --argc;
        ++argv;
    }
    if (!argc) {
        return cmd_results_new(CMD_INVALID, expected_position_syntax);
    }
    if (strcmp(argv[0], "position") == 0) {
        --argc;
        ++argv;
    }
    if (!argc) {
        return cmd_results_new(CMD_INVALID, expected_position_syntax);
    }
    if (strcmp(argv[0], "cursor") == 0 || strcmp(argv[0], "mouse") == 0 ||
        strcmp(argv[0], "pointer") == 0) {
        if (absolute) {
            return cmd_results_new(CMD_INVALID, expected_position_syntax);
        }
        return cmd_move_to_position_pointer(window);
    }

    if (argc < 2) {
        return cmd_results_new(CMD_FAILURE, expected_position_syntax);
    }

    struct movement_amount lx = {.amount = 0, .unit = MOVEMENT_UNIT_INVALID};
    // X direction
    int num_consumed_args = parse_movement_amount(argc, argv, &lx);
    argc -= num_consumed_args;
    argv += num_consumed_args;
    if (lx.unit == MOVEMENT_UNIT_INVALID) {
        return cmd_results_new(CMD_INVALID, "Invalid x position specified");
    }

    if (argc < 1) {
        return cmd_results_new(CMD_FAILURE, expected_position_syntax);
    }

    struct movement_amount ly = {.amount = 0, .unit = MOVEMENT_UNIT_INVALID};
    // Y direction
    num_consumed_args = parse_movement_amount(argc, argv, &ly);
    argc -= num_consumed_args;
    argv += num_consumed_args;
    if (argc > 0) {
        return cmd_results_new(CMD_INVALID, expected_position_syntax);
    }
    if (ly.unit == MOVEMENT_UNIT_INVALID) {
        return cmd_results_new(CMD_INVALID, "Invalid y position specified");
    }

    struct hwd_output *output = window->pending.output;

    switch (lx.unit) {
    case MOVEMENT_UNIT_PPT:
        if (absolute) {
            return cmd_results_new(CMD_FAILURE, "Cannot move to absolute positions by ppt");
        }
        // Convert to px
        lx.amount = output->pending.width * lx.amount / 100;
        lx.unit = MOVEMENT_UNIT_PX;
        // Falls through
    case MOVEMENT_UNIT_PX:
    case MOVEMENT_UNIT_DEFAULT:
        break;
    case MOVEMENT_UNIT_INVALID:
        hwd_abort("Invalid x unit");
    }

    switch (ly.unit) {
    case MOVEMENT_UNIT_PPT:
        if (absolute) {
            return cmd_results_new(CMD_FAILURE, "Cannot move to absolute positions by ppt");
        }
        // Convert to px
        ly.amount = output->pending.height * ly.amount / 100;
        ly.unit = MOVEMENT_UNIT_PX;
        // Falls through
    case MOVEMENT_UNIT_PX:
    case MOVEMENT_UNIT_DEFAULT:
        break;
    case MOVEMENT_UNIT_INVALID:
        hwd_abort("invalid y unit");
    }
    if (!absolute) {
        lx.amount += output->pending.x;
        ly.amount += output->pending.y;
    }

    window_floating_move_to(window, output, lx.amount, ly.amount);
    return cmd_results_new(CMD_SUCCESS, NULL);
}

static const char expected_full_syntax[] =
    "Expected "
    "'move left|right|up|down [<amount> [px]]'"
    " or 'move [window] [to] workspace"
    "  <name>|next|prev|next_on_output|prev_on_output|(number <num>)'"
    " or 'move [window] [to] output <name/id>|left|right|up|down'"
    " or 'move [window] [to] [absolute] position <x> [px] <y> [px]'"
    " or 'move [window] [to] [absolute] position center'"
    " or 'move [window] [to] position mouse|cursor|pointer'";

struct cmd_results *
cmd_move(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "move", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID, "Can't run this command while there's no outputs connected."
        );
    }

    if (strcasecmp(argv[0], "left") == 0) {
        return cmd_move_in_direction(WLR_DIRECTION_LEFT, --argc, ++argv);
    } else if (strcasecmp(argv[0], "right") == 0) {
        return cmd_move_in_direction(WLR_DIRECTION_RIGHT, --argc, ++argv);
    } else if (strcasecmp(argv[0], "up") == 0) {
        return cmd_move_in_direction(WLR_DIRECTION_UP, --argc, ++argv);
    } else if (strcasecmp(argv[0], "down") == 0) {
        return cmd_move_in_direction(WLR_DIRECTION_DOWN, --argc, ++argv);
    }

    if (argc > 0 && (strcasecmp(argv[0], "window") == 0)) {
        --argc;
        ++argv;
    }

    if (argc > 0 && strcasecmp(argv[0], "to") == 0) {
        --argc;
        ++argv;
    }

    if (!argc) {
        return cmd_results_new(CMD_INVALID, expected_full_syntax);
    }

    if (strcasecmp(argv[0], "workspace") == 0 || strcasecmp(argv[0], "output") == 0) {
        return cmd_move_window(argc, argv);
    } else if (strcasecmp(argv[0], "position") == 0 || (argc > 1 && strcasecmp(argv[0], "absolute") == 0 && strcasecmp(argv[1], "position") == 0)) {
        return cmd_move_to_position(argc, argv);
    }
    return cmd_results_new(CMD_INVALID, expected_full_syntax);
}