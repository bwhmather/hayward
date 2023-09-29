#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/tree/arrange.h"

#include <config.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/globals/root.h>
#include <hayward/output.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

void
arrange_window(struct hwd_window *window) {
    if (config->reloading) {
        return;
    }

    struct hwd_workspace *workspace = window->pending.workspace;
    struct hwd_output *output = workspace_get_active_output(workspace);

    if (window->pending.fullscreen) {
        window->pending.content_x = output->lx;
        window->pending.content_y = output->ly;
        window->pending.content_width = output->width;
        window->pending.content_height = output->height;
    } else {
        window->pending.border_top = window->pending.border_bottom = true;
        window->pending.border_left = window->pending.border_right = true;

        size_t border_thickness = window->pending.border_thickness;
        double titlebar_height = window_titlebar_height() + 2 * border_thickness;

        window->pending.content_x = window->pending.x + border_thickness;
        window->pending.content_y = window->pending.y + titlebar_height;
        window->pending.content_width = window->pending.width - 2 * border_thickness;
        window->pending.content_height =
            window->pending.height - titlebar_height - border_thickness;
    }

    window_set_dirty(window);
}

void
arrange_column(struct hwd_column *column) {
    struct wlr_box box;
    column_get_box(column, &box);

    list_t *children = column->pending.children;

    if (!children->length) {
        return;
    }

    // Count the number of new windows we are resizing, and how much space
    // is currently occupied
    int new_children = 0;
    double current_height_fraction = 0;
    for (int i = 0; i < children->length; ++i) {
        struct hwd_window *child = children->items[i];
        current_height_fraction += child->height_fraction;
        if (child->height_fraction <= 0) {
            new_children += 1;
        }
    }

    // Calculate each height fraction
    double total_height_fraction = 0;
    for (int i = 0; i < children->length; ++i) {
        struct hwd_window *child = children->items[i];
        if (child->height_fraction <= 0) {
            if (current_height_fraction <= 0) {
                child->height_fraction = 1.0;
            } else if (children->length > new_children) {
                child->height_fraction =
                    current_height_fraction / (children->length - new_children);
            } else {
                child->height_fraction = current_height_fraction;
            }
        }
        total_height_fraction += child->height_fraction;
    }
    // Normalize height fractions so the sum is 1.0
    for (int i = 0; i < children->length; ++i) {
        struct hwd_window *child = children->items[i];
        child->height_fraction /= total_height_fraction;
    }

    double child_total_height = box.height;

    // Resize windows
    double y_offset = 0;
    for (int i = 0; i < children->length; ++i) {
        struct hwd_window *child = children->items[i];
        child->child_total_height = child_total_height;
        child->pending.x = column->pending.x;
        child->pending.y = column->pending.y + y_offset;
        child->pending.width = box.width;
        child->pending.height = round(child->height_fraction * child_total_height);
        y_offset += child->pending.height;
        child->pending.shaded = false;

        // Make last child use remaining height of parent
        if (i == children->length - 1) {
            child->pending.height = box.height - child->pending.y;
        }
    }

    for (int i = 0; i < children->length; ++i) {
        struct hwd_window *child = children->items[i];
        arrange_window(child);
    }
    column_set_dirty(column);
}

static void
arrange_floating(struct hwd_workspace *workspace) {
    list_t *floating = workspace->pending.floating;
    for (int i = 0; i < floating->length; ++i) {
        struct hwd_window *floater = floating->items[i];
        floater->pending.shaded = false;
        arrange_window(floater);
    }
}

static void
arrange_tiling(struct hwd_workspace *workspace) {
    list_t *columns = workspace->pending.columns;
    if (!columns->length) {
        return;
    }

    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];

        struct wlr_box box;
        output_get_usable_area(output, &box);

        // Count the number of new columns we are resizing, and how much space
        // is currently occupied.
        int new_columns = 0;
        int total_columns = 0;
        double current_width_fraction = 0;
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            if (column->pending.output != output) {
                continue;
            }

            current_width_fraction += column->width_fraction;
            if (column->width_fraction <= 0) {
                new_columns += 1;
            }
            total_columns += 1;
        }

        // Calculate each width fraction.
        double total_width_fraction = 0;
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            if (column->pending.output != output) {
                continue;
            }

            if (column->width_fraction <= 0) {
                if (current_width_fraction <= 0) {
                    column->width_fraction = 1.0;
                } else if (total_columns > new_columns) {
                    column->width_fraction = current_width_fraction / (total_columns - new_columns);
                } else {
                    column->width_fraction = current_width_fraction;
                }
            }
            total_width_fraction += column->width_fraction;
        }
        // Normalize width fractions so the sum is 1.0.
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            if (column->pending.output != output) {
                continue;
            }
            column->width_fraction /= total_width_fraction;
        }

        double columns_total_width = box.width;

        // Resize columns.
        double column_x = box.x;
        for (int j = 0; j < columns->length; ++j) {
            struct hwd_column *column = columns->items[j];
            column->child_total_width = columns_total_width;
            column->pending.x = column_x;
            column->pending.y = box.y;
            column->pending.width = round(column->width_fraction * columns_total_width);
            column->pending.height = box.height;
            column_x += column->pending.width;

            // Make last child use remaining width of parent.
            if (j == total_columns - 1) {
                column->pending.width = box.x + box.width - column->pending.x;
            }
        }
    }

    for (int i = 0; i < columns->length; ++i) {
        struct hwd_column *column = columns->items[i];
        arrange_column(column);
    }
}

void
arrange_workspace(struct hwd_workspace *workspace) {
    if (config->reloading) {
        return;
    }

    hwd_log(HWD_DEBUG, "Arranging workspace '%s'", workspace->name);

    arrange_tiling(workspace);
    arrange_floating(workspace);

    workspace_set_dirty(workspace);
}

void
arrange_output(struct hwd_output *output) {
    if (config->reloading) {
        return;
    }
    struct wlr_box output_box;
    wlr_output_layout_get_box(root->output_layout, output->wlr_output, &output_box);
    output->lx = output_box.x;
    output->ly = output_box.y;
    output->width = output_box.width;
    output->height = output_box.height;

    if (output->pending.fullscreen_window) {
        struct hwd_window *fs = output->pending.fullscreen_window;
        fs->pending.x = output->lx;
        fs->pending.y = output->ly;
        fs->pending.width = output->width;
        fs->pending.height = output->height;
        arrange_window(fs);
    }
}

void
arrange_root(struct hwd_root *root) {
    if (config->reloading) {
        return;
    }

    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];
        arrange_output(output);
    }

    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hwd_workspace *workspace = root->pending.workspaces->items[i];
        arrange_workspace(workspace);
    }
}
