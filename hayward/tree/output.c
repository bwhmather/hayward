#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "hayward/ipc-server.h"
#include "hayward/layers.h"
#include "hayward/output.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/workspace.h"
#include "log.h"
#include "util.h"

enum wlr_direction opposite_direction(enum wlr_direction d) {
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
	assert(false);
	return 0;
}

struct hayward_output *output_create(struct wlr_output *wlr_output) {
	struct hayward_output *output = calloc(1, sizeof(struct hayward_output));
	node_init(&output->node, N_OUTPUT, output);
	output->wlr_output = wlr_output;
	wlr_output->data = output;
	output->detected_subpixel = wlr_output->subpixel;
	output->scale_filter = SCALE_FILTER_NEAREST;

	wl_signal_init(&output->events.disable);

	wl_list_insert(&root->all_outputs, &output->link);

	size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
	for (size_t i = 0; i < len; ++i) {
		wl_list_init(&output->layers[i]);
	}

	return output;
}

void output_enable(struct hayward_output *output) {
	hayward_assert(!output->enabled, "output is already enabled");
	output->enabled = true;
	list_add(root->outputs, output);
	if (root->pending.active_output == NULL || root->pending.active_output == root->fallback_output) {
		root->pending.active_output = output;
	}

	input_manager_configure_xcursor();

	wl_signal_emit(&root->events.new_node, &output->node);

	arrange_layers(output);
	arrange_root();
}

static void output_evacuate(struct hayward_output *output) {
	struct hayward_output *new_output = NULL;
	if (root->outputs->length > 1) {
		new_output = root->outputs->items[0];
		if (new_output == output) {
			new_output = root->outputs->items[1];
		}
	}

	for (int i = 0; i < root->pending.workspaces->length; i++) {
		struct hayward_workspace *workspace = root->pending.workspaces->items[i];

		// Move tiling windows.
		for (int j = 0; j < workspace->pending.tiling->length; j++) {
			struct hayward_column *column = workspace->pending.tiling->items[j];

			if (column->pending.output != output) {
				continue;
			}

			column->pending.output = new_output;
			for (int k = 0; k < column->pending.children->length; k++) {
				struct hayward_window *window = column->pending.children->items[k];

				window->pending.fullscreen = false;
				window->pending.output = output;

				ipc_event_window(window, "move");
			}
		}


		for (int j = 0; j < workspace->pending.floating->length; j++) {
			struct hayward_window *window = workspace->pending.floating->items[j];

			if (window->pending.output != output) {
				continue;
			}

			window->pending.fullscreen = false;
			window->pending.output = output;

			window_floating_move_to_center(window);

			ipc_event_window(window, "move");
		}

		arrange_workspace(workspace);
	}
}

void output_destroy(struct hayward_output *output) {
	hayward_assert(output->node.destroying,
				"Tried to free output which wasn't marked as destroying");
	hayward_assert(output->wlr_output == NULL,
				"Tried to free output which still had a wlr_output");
	hayward_assert(output->node.ntxnrefs == 0, "Tried to free output "
				"which is still referenced by transactions");
	wl_event_source_remove(output->repaint_timer);
	free(output);
}

static void untrack_output(struct hayward_window *container, void *data) {
	struct hayward_output *output = data;
	int index = list_find(container->outputs, output);
	if (index != -1) {
		list_del(container->outputs, index);
	}
}

void output_disable(struct hayward_output *output) {
	hayward_assert(output->enabled, "Expected an enabled output");

	int index = list_find(root->outputs, output);
	hayward_assert(index >= 0, "Output not found in root node");

	hayward_log(HAYWARD_DEBUG, "Disabling output '%s'", output->wlr_output->name);
	wl_signal_emit(&output->events.disable, output);

	output_evacuate(output);

	root_for_each_window(untrack_output, output);

	list_del(root->outputs, index);
	if (root->pending.active_output == output) {
		if (root->outputs->length == 0) {
			root->pending.active_output = root->fallback_output;
		} else {
			root->pending.active_output = root->outputs->items[index - 1 < 0 ? 0 : index - 1];
		}
	}

	output->enabled = false;
	output->current_mode = NULL;

	arrange_root();

	// Reconfigure all devices, since devices with map_to_output directives for
	// an output that goes offline should stop sending events as long as the
	// output remains offline.
	input_manager_configure_all_inputs();
}

void output_begin_destroy(struct hayward_output *output) {
	hayward_assert(!output->enabled, "Expected a disabled output");
	hayward_log(HAYWARD_DEBUG, "Destroying output '%s'", output->wlr_output->name);
	wl_signal_emit(&output->node.events.destroy, &output->node);

	output->node.destroying = true;
	node_set_dirty(&output->node);
}

struct hayward_output *output_from_wlr_output(struct wlr_output *output) {
	return output->data;
}

struct hayward_output *output_get_in_direction(struct hayward_output *reference,
		enum wlr_direction direction) {
	hayward_assert(direction, "got invalid direction: %d", direction);
	struct wlr_box output_box;
	wlr_output_layout_get_box(root->output_layout, reference->wlr_output, &output_box);
	int lx = output_box.x + output_box.width / 2;
	int ly = output_box.y + output_box.height / 2;
	struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
			root->output_layout, direction, reference->wlr_output, lx, ly);
	if (!wlr_adjacent) {
		return NULL;
	}
	return output_from_wlr_output(wlr_adjacent);
}

void output_get_box(struct hayward_output *output, struct wlr_box *box) {
	box->x = output->lx;
	box->y = output->ly;
	box->width = output->width;
	box->height = output->height;
}

void output_get_usable_area(struct hayward_output *output, struct wlr_box *box) {
	box->x = output->usable_area.x;
	box->y = output->usable_area.y;
	box->width = output->usable_area.width;
	box->height = output->usable_area.height;
}
