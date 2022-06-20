#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/tree/view.h"
#include "list.h"
#include "log.h"

void arrange_window(struct wmiiv_container *window) {
	if (config->reloading) {
		return;
	}
	view_autoconfigure(window->view);
	node_set_dirty(&window->node);
}

static void arrange_column_vert(struct wmiiv_container *column) {
	wmiiv_assert(container_is_column(column), "Expected column");
	wmiiv_assert(column->pending.layout == L_VERT, "Expected vertical column");

	struct wlr_box box;
	column_get_box(column, &box);

	list_t *children = column->pending.children;

	if (!children->length) {
		return;
	}

	struct wmiiv_workspace *workspace = column->pending.workspace;

	// Count the number of new windows we are resizing, and how much space
	// is currently occupied
	int new_children = 0;
	double current_height_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
		current_height_fraction += child->height_fraction;
		if (child->height_fraction <= 0) {
			new_children += 1;
		}
	}

	// Calculate each height fraction
	double total_height_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
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
		struct wmiiv_container *child = children->items[i];
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
		struct wmiiv_container *child = children->items[i];
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

static void arrange_column_tabbed(struct wmiiv_container *column) {
	wmiiv_assert(container_is_column(column), "Expected column");
	wmiiv_assert(column->pending.layout == L_TABBED, "Expected tabbed column");

	struct wlr_box box;
	column_get_box(column, &box);

	list_t *children = column->pending.children;

	if (!children->length) {
		return;
	}
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
		int parent_offset = child->view ? 0 : window_titlebar_height();
		child->pending.x = box.x;
		child->pending.y = box.y + parent_offset;
		child->pending.width = box.width;
		child->pending.height = box.height - parent_offset;
	}
}

static void arrange_column_stacked(struct wmiiv_container *column) {
	wmiiv_assert(container_is_column(column), "Expected column");
	wmiiv_assert(column->pending.layout == L_STACKED, "Expected stacked column");

	struct wlr_box box;
	column_get_box(column, &box);

	list_t *children = column->pending.children;

	if (!children->length) {
		return;
	}
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
		int parent_offset = child->view ?  0 :
			window_titlebar_height() * children->length;
		child->pending.x = box.x;
		child->pending.y = box.y + parent_offset;
		child->pending.width = box.width;
		child->pending.height = box.height - parent_offset;
	}
}

void arrange_column(struct wmiiv_container *column) {
	if (config->reloading) {
		return;
	}

	// Calculate x, y, width and height of children
	switch (column->pending.layout) {
	case L_VERT:
		arrange_column_vert(column);
		break;
	case L_TABBED:
		arrange_column_tabbed(column);
		break;
	case L_STACKED:
		arrange_column_stacked(column);
		break;
	default:
		wmiiv_assert(false, "Unsupported layout");
		break;
	}

	list_t *children = column->pending.children;
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
		arrange_window(child);
	}
	node_set_dirty(&column->node);
}

static void arrange_floating(struct wmiiv_workspace *workspace) {
	list_t *floating = workspace->floating;
	for (int i = 0; i < floating->length; ++i) {
		struct wmiiv_container *floater = floating->items[i];
		arrange_window(floater);
	}
}

static void arrange_tiling(struct wmiiv_workspace *workspace) {
	struct wlr_box box;
	workspace_get_box(workspace, &box);	

	list_t *children = workspace->tiling;

	if (!children->length) {
		return;
	}

	// Count the number of new windows we are resizing, and how much space
	// is currently occupied.
	int new_children = 0;
	double current_width_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
		current_width_fraction += child->width_fraction;
		if (child->width_fraction <= 0) {
			new_children += 1;
		}
	}

	// Calculate each height fraction.
	double total_width_fraction = 0;
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
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
		struct wmiiv_container *child = children->items[i];
		child->width_fraction /= total_width_fraction;
	}

	// Calculate gap size.
	double inner_gap = workspace->gaps_inner;
	double total_gap = fmin(inner_gap * (children->length - 1),
		fmax(0, box.width - MIN_SANE_W * children->length));
	double child_total_width = box.width - total_gap;
	inner_gap = floor(total_gap / (children->length - 1));

	// Resize windows.
	double child_x = box.x;
	for (int i = 0; i < children->length; ++i) {
		struct wmiiv_container *child = children->items[i];
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
		struct wmiiv_container *child = children->items[i];
		arrange_column(child);
	}
}


void arrange_workspace(struct wmiiv_workspace *workspace) {
	if (config->reloading) {
		return;
	}
	if (!workspace->output) {
		// Happens when there are no outputs connected.
		return;
	}
	struct wmiiv_output *output = workspace->output;
	struct wlr_box *area = &output->usable_area;
	wmiiv_log(WMIIV_DEBUG, "Usable area for workspace: %dx%d@%d,%d",
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
			struct wmiiv_container *floater = workspace->floating->items[i];
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
	wmiiv_log(WMIIV_DEBUG, "Arranging workspace '%s' at %f, %f", workspace->name,
			workspace->x, workspace->y);
	if (workspace->fullscreen) {
		struct wmiiv_container *fs = workspace->fullscreen;
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

void arrange_output(struct wmiiv_output *output) {
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
		struct wmiiv_workspace *workspace = output->workspaces->items[i];
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

	if (root->fullscreen_global) {
		struct wmiiv_container *fs = root->fullscreen_global;
		fs->pending.x = root->x;
		fs->pending.y = root->y;
		fs->pending.width = root->width;
		fs->pending.height = root->height;
		arrange_window(fs);
	} else {
		for (int i = 0; i < root->outputs->length; ++i) {
			struct wmiiv_output *output = root->outputs->items[i];
			arrange_output(output);
		}
	}
}

void arrange_node(struct wmiiv_node *node) {
	switch (node->type) {
	case N_ROOT:
		arrange_root();
		break;
	case N_OUTPUT:
		arrange_output(node->wmiiv_output);
		break;
	case N_WORKSPACE:
		arrange_workspace(node->wmiiv_workspace);
		break;
	case N_COLUMN:
		arrange_column(node->wmiiv_container);
		break;
	case N_WINDOW:
		arrange_window(node->wmiiv_container);
		break;
	}
}
