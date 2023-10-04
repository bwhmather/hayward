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
    struct hwd_window *child = NULL;
    list_t *children = column->pending.children;

    if (!children->length) {
        column->active_height_fraction = 0.0;
        column->pending.preview_target = NULL;
        column->pending.preview_box.x = column->pending.x;
        column->pending.preview_box.y = column->pending.y;
        column->pending.preview_box.width = column->pending.width;
        column->pending.preview_box.height = column->pending.height;
        return;
    }

    int titlebar_height = window_titlebar_height() + 2 * config->border_thickness;
    int num_titlebars = children->length;
    if (column->pending.show_preview) {
        num_titlebars += 1;
    }

    struct wlr_box box;
    column_get_box(column, &box);
    double available_content_height = box.height - (num_titlebars * titlebar_height);

    double allocated_content_height = 0.0;
    // Number of windows that should have height allocated.
    int num_eligible = 1;
    // Number of those windows that do not currently have height allocated.
    int num_unallocated = 0;
    if (column->active_height_fraction != 0.0) {
        allocated_content_height = column->active_height_fraction;
    } else {
        num_unallocated += 1;
    }
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        if (!child->pending.pinned) {
            continue;
        }
        num_eligible += 1;
        if (child->height_fraction != 0.0) {
            allocated_content_height += child->height_fraction;
        } else {
            num_unallocated += 1;
        }
    }

    // Assign a default height for pinned windows if not already set.
    double default_height;
    if (num_unallocated == num_eligible) {
        default_height = available_content_height / ((double)num_unallocated);
    } else {
        default_height = allocated_content_height / ((double)(num_eligible - num_unallocated));
    }

    if (column->active_height_fraction == 0.0) {
        column->active_height_fraction = default_height;
        allocated_content_height += default_height;
    }
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        if (!child->pending.pinned) {
            continue;
        }
        if (child->height_fraction != 0.0) {
            continue;
        }
        child->height_fraction = default_height;
        allocated_content_height += default_height;
    }
    if (column->preview_height_fraction == 0.0) {
        column->preview_height_fraction = default_height;
    }

    // Normalize height fractions.
    column->active_height_fraction *= available_content_height / allocated_content_height;
    column->preview_height_fraction *= available_content_height / allocated_content_height;
    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];
        child->height_fraction *= available_content_height / allocated_content_height;
    }
    allocated_content_height = available_content_height;

    // Check if currently focused window is pinned.
    struct hwd_window *active_child = column->pending.active_child;
    if (column->pending.show_preview) {
        // Preview window replaces un-pinned focused windows.
        active_child = NULL;
        allocated_content_height += column->preview_height_fraction;
    }
    if (active_child == NULL || active_child->pending.pinned) {
        allocated_content_height -= column->active_height_fraction;
    }

    // Distance between top of next window and top of the screen.
    double y_offset = 0;

    // The distance, in layout coordinates, between the desired location of the
    // vertical anchor point in the preview and the top of the preview.
    double preview_baseline = round(column->preview_baseline * column->preview_height_fraction);

    // Absolute distance between preview baseline and anchor point if preview is
    // inserted before this one.
    double baseline_delta;

    // Absolute distance between preview baseline and anchor point if preview is
    // inserted after this one.
    double next_baseline_delta;

    bool preview_inserted = false;

    next_baseline_delta = fabs(column->pending.y + preview_baseline - column->preview_anchor_y);

    for (int i = 0; i < children->length; ++i) {
        child = children->items[i];

        double window_height = (double)titlebar_height;
        if (!child->pending.pinned && child != active_child) {
            child->pending.shaded = true;
        } else {
            double height_fraction = child->height_fraction;
            if (!child->pending.pinned) {
                height_fraction = column->active_height_fraction;
            }
            window_height += height_fraction * available_content_height / allocated_content_height;
            child->pending.shaded = false;
        }

        baseline_delta = next_baseline_delta;
        next_baseline_delta = fabs(
            column->pending.y + round(y_offset + window_height) + preview_baseline -
            column->preview_anchor_y
        );
        if (column->pending.show_preview && !preview_inserted &&
            next_baseline_delta > baseline_delta) {

            double preview_height = (double)titlebar_height;
            preview_height += column->preview_height_fraction * available_content_height /
                allocated_content_height;

            column->pending.preview_target = window_get_previous_sibling(child);
            column->pending.preview_box.x = column->pending.x;
            column->pending.preview_box.y = column->pending.y + round(y_offset);
            column->pending.preview_box.width = column->pending.width;
            column->pending.preview_box.height = round(preview_height);

            preview_inserted = true;

            y_offset += preview_height;
        }

        child->pending.x = column->pending.x;
        child->pending.y = column->pending.y + round(y_offset);
        child->pending.width = box.width;
        child->pending.height = round(window_height);

        y_offset += child->pending.height;

        // TODO Make last visible child use remaining height of parent
    }

    if (column->pending.show_preview && !preview_inserted) {
        double preview_height = (double)titlebar_height;
        preview_height +=
            column->preview_height_fraction * available_content_height / allocated_content_height;

        column->pending.preview_target = child;
        column->pending.preview_box.x = column->pending.x;
        column->pending.preview_box.y = column->pending.y + round(y_offset);
        column->pending.preview_box.width = column->pending.width;
        column->pending.preview_box.height = round(preview_height);

        preview_inserted = true;

        y_offset += round(preview_height);
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
