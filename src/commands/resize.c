#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include <wlr/util/edges.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>
#include <hayward/util.h>

#define AXIS_HORIZONTAL (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)
#define AXIS_VERTICAL (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)

static uint32_t
parse_resize_axis(const char *axis) {
    if (strcasecmp(axis, "width") == 0 || strcasecmp(axis, "horizontal") == 0) {
        return AXIS_HORIZONTAL;
    }
    if (strcasecmp(axis, "height") == 0 || strcasecmp(axis, "vertical") == 0) {
        return AXIS_VERTICAL;
    }
    if (strcasecmp(axis, "up") == 0) {
        return WLR_EDGE_TOP;
    }
    if (strcasecmp(axis, "down") == 0) {
        return WLR_EDGE_BOTTOM;
    }
    if (strcasecmp(axis, "left") == 0) {
        return WLR_EDGE_LEFT;
    }
    if (strcasecmp(axis, "right") == 0) {
        return WLR_EDGE_RIGHT;
    }
    return WLR_EDGE_NONE;
}

static bool
is_horizontal(uint32_t axis) {
    return axis & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
}

static void
window_resize_tiled_horizontal(struct hwd_window *window, uint32_t axis, int amount) {
    if (!window) {
        return;
    }

    struct hwd_workspace *workspace = window->workspace;
    struct hwd_column *column = window->column;

    struct hwd_column *prev_sibling = NULL;
    struct hwd_column *next_sibling = NULL;
    if (axis & WLR_EDGE_LEFT) {
        prev_sibling = workspace_get_column_before(workspace, column);
    }
    if (axis & WLR_EDGE_RIGHT) {
        next_sibling = workspace_get_column_after(workspace, column);
    }

    if (prev_sibling == NULL && next_sibling == NULL) {
        return;
    }

    int prev_amount = amount;
    int next_amount = amount;
    if (prev_sibling != NULL && next_sibling != NULL) {
        prev_amount = ceil((double)amount / 2.0);
        next_amount = amount - prev_amount;
    }

    if (column->pending.width + amount < MIN_SANE_W) {
        return;
    }
    if (prev_sibling != NULL && prev_sibling->pending.width - prev_amount < MIN_SANE_W) {
        return;
    }
    if (next_sibling != NULL && next_sibling->pending.width - next_amount < MIN_SANE_W) {
        return;
    }

    // We're going to resize so snap all the width fractions to full pixels
    // to avoid rounding issues
    list_t *siblings = workspace->columns;
    for (int i = 0; i < siblings->length; ++i) {
        struct hwd_column *sibling = siblings->items[i];
        sibling->width_fraction = sibling->pending.width / sibling->child_total_width;
    }

    column->width_fraction += (double)amount / column->child_total_width;
    if (prev_sibling != NULL) {
        prev_sibling->width_fraction -= (double)prev_amount / prev_sibling->child_total_width;
    }
    if (next_sibling != NULL) {
        next_sibling->width_fraction -= (double)next_amount / next_sibling->child_total_width;
    }

    workspace_set_dirty(column->workspace);
}

static void
window_resize_tiled_vertical(struct hwd_window *window, uint32_t axis, int amount) {
    if (!window) {
        return;
    }

    struct hwd_column *column = window->column;
    if (column->layout != L_SPLIT) {
        return;
    }

    struct hwd_window *prev_sibling = NULL;
    struct hwd_window *next_sibling = NULL;
    if (axis & WLR_EDGE_TOP) {
        prev_sibling = window_get_previous_sibling(window);
    }
    if (axis & WLR_EDGE_BOTTOM) {
        next_sibling = window_get_next_sibling(window);
    }

    if (prev_sibling == NULL && next_sibling == NULL) {
        return;
    }

    int prev_amount = amount;
    int next_amount = amount;
    if (prev_sibling != NULL && next_sibling != NULL) {
        prev_amount = ceil((double)amount / 2.0);
        next_amount = amount - prev_amount;
    }

    if (window->pending.height + amount < MIN_SANE_W) {
        return;
    }
    if (prev_sibling != NULL && prev_sibling->pending.height - prev_amount < MIN_SANE_W) {
        return;
    }
    if (next_sibling != NULL && next_sibling->pending.height - next_amount < MIN_SANE_W) {
        return;
    }

    double visible_height_fraction = 1.0;
    double available_content_height = column->pending.height;
    list_t *siblings = column->children;
    for (int i = 0; i < siblings->length; ++i) {
        struct hwd_window *sibling = siblings->items[i];
        available_content_height -= sibling->pending.titlebar_height;
    }

    window->height_fraction +=
        (double)amount / (available_content_height * visible_height_fraction);
    if (prev_sibling != NULL) {
        prev_sibling->height_fraction -=
            (double)prev_amount / (available_content_height * visible_height_fraction);
    }
    if (next_sibling != NULL) {
        next_sibling->height_fraction -=
            (double)next_amount / (available_content_height * visible_height_fraction);
    }

    column_set_dirty(column);
}

void
window_resize_tiled(struct hwd_window *window, uint32_t axis, int amount) {
    if (!window) {
        return;
    }

    if (axis & AXIS_HORIZONTAL) {
        window_resize_tiled_horizontal(window, axis, amount);
    }

    if (axis & AXIS_VERTICAL) {
        window_resize_tiled_vertical(window, axis, amount);
    }
}

/**
 * Implement `resize <grow|shrink>` for a floating window.
 */
static struct cmd_results *
resize_adjust_floating(uint32_t axis, struct movement_amount *amount) {
    struct hwd_window *window = config->handler_context.window;
    int grow_width = 0, grow_height = 0;

    if (is_horizontal(axis)) {
        grow_width = amount->amount;
    } else {
        grow_height = amount->amount;
    }

    // Make sure we're not adjusting beyond floating min/max size
    int min_width, max_width, min_height, max_height;
    floating_calculate_constraints(window, &min_width, &max_width, &min_height, &max_height);
    if (window->pending.width + grow_width < min_width) {
        grow_width = min_width - window->pending.width;
    } else if (window->pending.width + grow_width > max_width) {
        grow_width = max_width - window->pending.width;
    }
    if (window->pending.height + grow_height < min_height) {
        grow_height = min_height - window->pending.height;
    } else if (window->pending.height + grow_height > max_height) {
        grow_height = max_height - window->pending.height;
    }
    int grow_x = 0, grow_y = 0;

    if (axis == AXIS_HORIZONTAL) {
        grow_x = -grow_width / 2;
    } else if (axis == AXIS_VERTICAL) {
        grow_y = -grow_height / 2;
    } else if (axis == WLR_EDGE_TOP) {
        grow_y = -grow_height;
    } else if (axis == WLR_EDGE_LEFT) {
        grow_x = -grow_width;
    }
    if (grow_width == 0 && grow_height == 0) {
        return cmd_results_new(CMD_INVALID, "Cannot resize any further");
    }
    window->pending.x += grow_x;
    window->pending.y += grow_y;
    window->pending.width += grow_width;
    window->pending.height += grow_height;

    window->pending.content_x += grow_x;
    window->pending.content_y += grow_y;
    window->pending.content_width += grow_width;
    window->pending.content_height += grow_height;

    window_set_dirty(window);

    return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize <grow|shrink>` for a tiled window.
 */
static struct cmd_results *
resize_adjust_tiled(uint32_t axis, struct movement_amount *amount) {
    struct hwd_window *current = config->handler_context.window;

    if (amount->unit == MOVEMENT_UNIT_DEFAULT) {
        amount->unit = MOVEMENT_UNIT_PPT;
    }
    if (amount->unit == MOVEMENT_UNIT_PPT) {
        float pct = amount->amount / 100.0f;

        if (is_horizontal(axis)) {
            amount->amount = (float)current->pending.width * pct;
        } else {
            amount->amount = (float)current->pending.height * pct;
        }
    }

    double old_height = current->height_fraction;
    window_resize_tiled(current, axis, amount->amount);
    if (current->height_fraction == old_height) {
        return cmd_results_new(CMD_INVALID, "Cannot resize any further");
    }
    return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize set` for a tiled window.
 */
static struct cmd_results *
resize_set_tiled(
    struct hwd_window *window, struct movement_amount *width, struct movement_amount *height
) {
    struct hwd_output *output = window_get_output(window);

    if (width->amount) {
        if (width->unit == MOVEMENT_UNIT_PPT || width->unit == MOVEMENT_UNIT_DEFAULT) {
            // Convert to px
            struct hwd_column *column = window->column;
            width->amount = column->pending.width * width->amount / 100;
            width->unit = MOVEMENT_UNIT_PX;
        }
        if (width->unit == MOVEMENT_UNIT_PX) {
            window_resize_tiled(window, AXIS_HORIZONTAL, width->amount - window->pending.width);
        }
    }

    if (height->amount) {
        if (height->unit == MOVEMENT_UNIT_PPT || height->unit == MOVEMENT_UNIT_DEFAULT) {
            // Convert to px
            height->amount = output->pending.height * height->amount / 100;
            height->unit = MOVEMENT_UNIT_PX;
        }
        if (height->unit == MOVEMENT_UNIT_PX) {
            window_resize_tiled(window, AXIS_VERTICAL, height->amount - window->pending.height);
        }
    }

    return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * Implement `resize set` for a floating window.
 */
static struct cmd_results *
resize_set_floating(
    struct hwd_window *window, struct movement_amount *width, struct movement_amount *height
) {
    struct hwd_output *output = window_get_output(window);

    int min_width, max_width, min_height, max_height, grow_width = 0, grow_height = 0;
    floating_calculate_constraints(window, &min_width, &max_width, &min_height, &max_height);

    if (width->amount) {
        switch (width->unit) {
        case MOVEMENT_UNIT_PPT:
            // Convert to px
            width->amount = output->pending.width * width->amount / 100;
            width->unit = MOVEMENT_UNIT_PX;
            // Falls through
        case MOVEMENT_UNIT_PX:
        case MOVEMENT_UNIT_DEFAULT:
            width->amount = fmax(min_width, fmin(width->amount, max_width));
            grow_width = width->amount - window->pending.width;
            window->pending.x -= grow_width / 2;
            window->pending.width = width->amount;
            break;
        case MOVEMENT_UNIT_INVALID:
            assert(false);
            break;
        }
    }

    if (height->amount) {
        switch (height->unit) {
        case MOVEMENT_UNIT_PPT:
            // Convert to px
            height->unit = MOVEMENT_UNIT_PX;
            // Falls through
        case MOVEMENT_UNIT_PX:
        case MOVEMENT_UNIT_DEFAULT:
            height->amount = fmax(min_height, fmin(height->amount, max_height));
            grow_height = height->amount - window->pending.height;
            window->pending.y -= grow_height / 2;
            window->pending.height = height->amount;
            break;
        case MOVEMENT_UNIT_INVALID:
            assert(false);
            break;
        }
    }

    window->pending.content_x -= grow_width / 2;
    window->pending.content_y -= grow_height / 2;
    window->pending.content_width += grow_width;
    window->pending.content_height += grow_height;

    window_set_dirty(window);

    return cmd_results_new(CMD_SUCCESS, NULL);
}

/**
 * resize set <args>
 *
 * args: [width] <width> [px|ppt]
 *     : height <height> [px|ppt]
 *     : [width] <width> [px|ppt] [height] <height> [px|ppt]
 */
static struct cmd_results *
cmd_resize_set(int argc, char **argv) {
    struct cmd_results *error;
    if ((error = checkarg(argc, "resize", EXPECTED_AT_LEAST, 1))) {
        return error;
    }
    const char usage[] = "Expected 'resize set [width] <width> [px|ppt]' or "
                         "'resize set height <height> [px|ppt]' or "
                         "'resize set [width] <width> [px|ppt] [height] <height> [px|ppt]'";

    // Width
    struct movement_amount width = {0};
    if (argc >= 2 && !strcmp(argv[0], "width") && strcmp(argv[1], "height")) {
        argc--;
        argv++;
    }
    if (strcmp(argv[0], "height")) {
        int num_consumed_args = parse_movement_amount(argc, argv, &width);
        argc -= num_consumed_args;
        argv += num_consumed_args;
        if (width.unit == MOVEMENT_UNIT_INVALID) {
            return cmd_results_new(CMD_INVALID, usage);
        }
    }

    // Height
    struct movement_amount height = {0};
    if (argc) {
        if (argc >= 2 && !strcmp(argv[0], "height")) {
            argc--;
            argv++;
        }
        int num_consumed_args = parse_movement_amount(argc, argv, &height);
        if (argc > num_consumed_args) {
            return cmd_results_new(CMD_INVALID, usage);
        }
        if (width.unit == MOVEMENT_UNIT_INVALID) {
            return cmd_results_new(CMD_INVALID, usage);
        }
    }

    // If 0, don't resize that dimension
    struct hwd_window *window = config->handler_context.window;
    if (width.amount <= 0) {
        width.amount = window->pending.width;
    }
    if (height.amount <= 0) {
        height.amount = window->pending.height;
    }

    if (window_is_floating(window)) {
        return resize_set_floating(window, &width, &height);
    } else {
        return resize_set_tiled(window, &width, &height);
    }
}

/**
 * resize <grow|shrink> <args>
 *
 * args: <direction>
 * args: <direction> <amount> <unit>
 * args: <direction> <amount> <unit> or <amount> <other_unit>
 */
static struct cmd_results *
cmd_resize_adjust(int argc, char **argv, int multiplier) {
    const char usage[] = "Expected 'resize grow|shrink <direction> "
                         "[<amount> px|ppt [or <amount> px|ppt]]'";
    uint32_t axis = parse_resize_axis(*argv);
    if (axis == WLR_EDGE_NONE) {
        return cmd_results_new(CMD_INVALID, usage);
    }
    --argc;
    ++argv;

    // First amount
    struct movement_amount first_amount;
    if (argc) {
        int num_consumed_args = parse_movement_amount(argc, argv, &first_amount);
        argc -= num_consumed_args;
        argv += num_consumed_args;
        if (first_amount.unit == MOVEMENT_UNIT_INVALID) {
            return cmd_results_new(CMD_INVALID, usage);
        }
    } else {
        first_amount.amount = 10;
        first_amount.unit = MOVEMENT_UNIT_DEFAULT;
    }

    // "or"
    if (argc) {
        if (strcmp(*argv, "or") != 0) {
            return cmd_results_new(CMD_INVALID, usage);
        }
        --argc;
        ++argv;
    }

    // Second amount
    struct movement_amount second_amount;
    if (argc) {
        int num_consumed_args = parse_movement_amount(argc, argv, &second_amount);
        if (argc > num_consumed_args) {
            return cmd_results_new(CMD_INVALID, usage);
        }
        if (second_amount.unit == MOVEMENT_UNIT_INVALID) {
            return cmd_results_new(CMD_INVALID, usage);
        }
    } else {
        second_amount.amount = 0;
        second_amount.unit = MOVEMENT_UNIT_INVALID;
    }

    first_amount.amount *= multiplier;
    second_amount.amount *= multiplier;

    struct hwd_window *window = config->handler_context.window;
    if (window && window_is_floating(window)) {
        // Floating windows can only resize in px. Choose an amount which
        // uses px, with fallback to an amount that specified no unit.
        if (first_amount.unit == MOVEMENT_UNIT_PX) {
            return resize_adjust_floating(axis, &first_amount);
        } else if (second_amount.unit == MOVEMENT_UNIT_PX) {
            return resize_adjust_floating(axis, &second_amount);
        } else if (first_amount.unit == MOVEMENT_UNIT_DEFAULT) {
            return resize_adjust_floating(axis, &first_amount);
        } else if (second_amount.unit == MOVEMENT_UNIT_DEFAULT) {
            return resize_adjust_floating(axis, &second_amount);
        } else {
            return cmd_results_new(CMD_INVALID, "Floating windows cannot use ppt measurements");
        }
    }

    // For tiling, prefer ppt -> default -> px
    if (first_amount.unit == MOVEMENT_UNIT_PPT) {
        return resize_adjust_tiled(axis, &first_amount);
    } else if (second_amount.unit == MOVEMENT_UNIT_PPT) {
        return resize_adjust_tiled(axis, &second_amount);
    } else if (first_amount.unit == MOVEMENT_UNIT_DEFAULT) {
        return resize_adjust_tiled(axis, &first_amount);
    } else if (second_amount.unit == MOVEMENT_UNIT_DEFAULT) {
        return resize_adjust_tiled(axis, &second_amount);
    } else {
        return resize_adjust_tiled(axis, &first_amount);
    }
}

struct cmd_results *
cmd_resize(int argc, char **argv) {
    HWD_PROFILER_TRACE();

    if (!root->outputs->length) {
        return cmd_results_new(
            CMD_INVALID, "Can't run this command while there's no outputs connected."
        );
    }
    struct hwd_window *current = config->handler_context.window;
    if (!current) {
        return cmd_results_new(CMD_INVALID, "Cannot resize nothing");
    }

    struct cmd_results *error;
    if ((error = checkarg(argc, "resize", EXPECTED_AT_LEAST, 2))) {
        return error;
    }

    if (strcasecmp(argv[0], "set") == 0) {
        return cmd_resize_set(argc - 1, &argv[1]);
    }
    if (strcasecmp(argv[0], "grow") == 0) {
        return cmd_resize_adjust(argc - 1, &argv[1], 1);
    }
    if (strcasecmp(argv[0], "shrink") == 0) {
        return cmd_resize_adjust(argc - 1, &argv[1], -1);
    }

    const char usage[] = "Expected 'resize <shrink|grow> "
                         "<width|height|up|down|left|right> [<amount>] [px|ppt]'";

    return cmd_results_new(CMD_INVALID, usage);
}
