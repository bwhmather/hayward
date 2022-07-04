#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "hayward/tree/arrange.h"
#include "hayward/tree/column.h"
#include "hayward/tree/window.h"
#include "hayward/output.h"
#include "hayward/tree/workspace.h"
#include "hayward/tree/view.h"
#include "list.h"
#include "log.h"

void arrange_window(struct hayward_window *window) {
	if (config->reloading) {
		return;
	}
	view_autoconfigure(window->view);
	node_set_dirty(&window->node);
}

static void arrange_column_split(struct hayward_column *column) {
	hayward_assert(column->pending.layout == L_SPLIT, "Expected split column");

	struct wlr_box box;
	column_get_box(column, &box);

	list_t *children = column->pending.children;

	if (!children->length) {
		return;
	}

	struct hayward_workspace *workspace = column->pending.workspace;

	// Count the number of new windows we are resizing, and how much space
	// is currently occupied
	int new_children = 0;
	double current_height_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_window *child = children->items[i];
		current_height_fraction += child->height_fraction;
		if (child->height_fraction <= 0) {
			new_children += 1;
		}
	}

	// Calculate each height fraction
	double total_height_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_window *child = children->items[i];
		if (child->height_fraction <= 0) {
			if (current_height_fraction <= 0) {
				child->height_fraction = 1.0;
			} else if (children->length > new_children) {
				child->height_fraction = current_height_fraction /
					(children->length - new_children);
			} else {
				child->height_fraction = current_height_fraction;
			}
		}
		total_height_fraction += child->height_fraction;
	}
	// Normalize height fractions so the sum is 1.0
	for (int i = 0; i < children->length; ++i) {
		struct hayward_window *child = children->items[i];
		child->height_fraction /= total_height_fraction;
	}

	// Calculate gap size
	double inner_gap = workspace->gaps_inner;
	double total_gap = fmin(inner_gap * (children->length - 1),
		fmax(0, box.height - MIN_SANE_H * children->length));
	double child_total_height = box.height - total_gap;
	inner_gap = floor(total_gap / (children->length - 1));

	// Resize windows
	double child_y = box.y;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_window *child = children->items[i];
		child->child_total_height = child_total_height;
		child->pending.x = box.x;
		child->pending.y = child_y;
		child->pending.width = box.width;
		child->pending.height = round(child->height_fraction * child_total_height);
		child_y += child->pending.height + inner_gap;

		// Make last child use remaining height of parent
		if (i == children->length - 1) {
			child->pending.height = box.y + box.height - child->pending.y;
		}
	}
}

static void arrange_column_stacked(struct hayward_column *column) {
	hayward_assert(column->pending.layout == L_STACKED, "Expected stacked column");

	struct wlr_box box;
	column_get_box(column, &box);

	list_t *children = column->pending.children;

	if (!children->length) {
		return;
	}
	for (int i = 0; i < children->length; ++i) {
		struct hayward_window *child = children->items[i];
		int parent_offset = child->view ?  0 :
			window_titlebar_height() * children->length;
		child->pending.x = box.x;
		child->pending.y = box.y + parent_offset;
		child->pending.width = box.width;
		child->pending.height = box.height - parent_offset;
	}
}

void arrange_column(struct hayward_column *column) {
	if (config->reloading) {
		return;
	}

	// Calculate x, y, width and height of children
	switch (column->pending.layout) {
	case L_SPLIT:
		arrange_column_split(column);
		break;
	case L_STACKED:
		arrange_column_stacked(column);
		break;
	default:
		hayward_assert(false, "Unsupported layout");
		break;
	}

	list_t *children = column->pending.children;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_window *child = children->items[i];
		arrange_window(child);
	}
	node_set_dirty(&column->node);
}

static void arrange_floating(struct hayward_workspace *workspace) {
	list_t *floating = workspace->floating;
	for (int i = 0; i < floating->length; ++i) {
		struct hayward_window *floater = floating->items[i];
		arrange_window(floater);
	}
}

static void arrange_tiling(struct hayward_workspace *workspace) {
	struct wlr_box box;
	workspace_get_box(workspace, &box);	

	list_t *children = workspace->tiling;

	if (!children->length) {
		return;
	}

	// Count the number of new columns we are resizing, and how much space
	// is currently occupied.
	int new_children = 0;
	double current_width_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_column *child = children->items[i];
		current_width_fraction += child->width_fraction;
		if (child->width_fraction <= 0) {
			new_children += 1;
		}
	}

	// Calculate each width fraction.
	double total_width_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_column *child = children->items[i];
		if (child->width_fraction <= 0) {
			if (current_width_fraction <= 0) {
				child->width_fraction = 1.0;
			} else if (children->length > new_children) {
				child->width_fraction = current_width_fraction /
					(children->length - new_children);
			} else {
				child->width_fraction = current_width_fraction;
			}
		}
		total_width_fraction += child->width_fraction;
	}
	// Normalize width fractions so the sum is 1.0.
	for (int i = 0; i < children->length; ++i) {
		struct hayward_column *child = children->items[i];
		child->width_fraction /= total_width_fraction;
	}

	// Calculate gap size.
	double inner_gap = workspace->gaps_inner;
	double total_gap = fmin(inner_gap * (children->length - 1),
		fmax(0, box.width - MIN_SANE_W * children->length));
	double child_total_width = box.width - total_gap;
	inner_gap = floor(total_gap / (children->length - 1));

	// Resize columns.
	double child_x = box.x;
	for (int i = 0; i < children->length; ++i) {
		struct hayward_column *child = children->items[i];
		child->child_total_width = child_total_width;
		child->pending.x = child_x;
		child->pending.y = box.y;
		child->pending.width = round(child->width_fraction * child_total_width);
		child->pending.height = box.height;
		child_x += child->pending.width + inner_gap;

		// Make last child use remaining width of parent.
		if (i == children->length - 1) {
			child->pending.width = box.x + box.width - child->pending.x;
		}
	}

	for (int i = 0; i < children->length; ++i) {
		struct hayward_column *child = children->items[i];
		arrange_column(child);
	}
}


void arrange_workspace(struct hayward_workspace *workspace) {
	if (config->reloading) {
		return;
	}
	if (!workspace->output) {
		// Happens when there are no outputs connected.
		return;
	}
	struct hayward_output *output = workspace->output;
	struct wlr_box *area = &output->usable_area;
	hayward_log(HAYWARD_DEBUG, "Usable area for workspace: %dx%d@%d,%d",
			area->width, area->height, area->x, area->y);

	bool first_arrange = workspace->width == 0 && workspace->height == 0;
	double prev_x = workspace->x - workspace->current_gaps.left;
	double prev_y = workspace->y - workspace->current_gaps.top;
	workspace->width = area->width;
	workspace->height = area->height;
	workspace->x = output->lx + area->x;
	workspace->y = output->ly + area->y;

	// Adjust any floating containers.
	double diff_x = workspace->x - prev_x;
	double diff_y = workspace->y - prev_y;
	if (!first_arrange && (diff_x != 0 || diff_y != 0)) {
		for (int i = 0; i < workspace->floating->length; ++i) {
			struct hayward_window *floater = workspace->floating->items[i];
			window_floating_translate(floater, diff_x, diff_y);
			double center_x = floater->pending.x + floater->pending.width / 2;
			double center_y = floater->pending.y + floater->pending.height / 2;
			struct wlr_box workspace_box;
			workspace_get_box(workspace, &workspace_box);
			if (!wlr_box_contains_point(&workspace_box, center_x, center_y)) {
				window_floating_move_to_center(floater);
			}
		}
	}

	workspace_add_gaps(workspace);
	node_set_dirty(&workspace->node);
	hayward_log(HAYWARD_DEBUG, "Arranging workspace '%s' at %f, %f", workspace->name,
			workspace->x, workspace->y);
	if (workspace->fullscreen) {
		struct hayward_window *fs = workspace->fullscreen;
		fs->pending.x = output->lx;
		fs->pending.y = output->ly;
		fs->pending.width = output->width;
		fs->pending.height = output->height;
		arrange_window(fs);
	} else {
		arrange_tiling(workspace);
		arrange_floating(workspace);
	}
}

void arrange_output(struct hayward_output *output) {
	if (config->reloading) {
		return;
	}
	struct wlr_box output_box;
	wlr_output_layout_get_box(root->output_layout,
		output->wlr_output, &output_box);
	output->lx = output_box.x;
	output->ly = output_box.y;
	output->width = output_box.width;
	output->height = output_box.height;

	for (int i = 0; i < output->workspaces->length; ++i) {
		struct hayward_workspace *workspace = output->workspaces->items[i];
		arrange_workspace(workspace);
	}
}

void arrange_root(void) {
	if (config->reloading) {
		return;
	}
	struct wlr_box layout_box;
	wlr_output_layout_get_box(root->output_layout, NULL, &layout_box);
	root->x = layout_box.x;
	root->y = layout_box.y;
	root->width = layout_box.width;
	root->height = layout_box.height;

	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		arrange_output(output);
	}
}

void arrange_node(struct hayward_node *node) {
	switch (node->type) {
	case N_ROOT:
		arrange_root();
		break;
	case N_OUTPUT:
		arrange_output(node->hayward_output);
		break;
	case N_WORKSPACE:
		arrange_workspace(node->hayward_workspace);
		break;
	case N_COLUMN:
		arrange_column(node->hayward_column);
		break;
	case N_WINDOW:
		arrange_window(node->hayward_window);
		break;
	}
}
