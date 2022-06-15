#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <wlr/types/wlr_output_damage.h>
#include "wmiiv/ipc-server.h"
#include "wmiiv/layers.h"
#include "wmiiv/output.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/workspace.h"
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

static void restore_workspaces(struct wmiiv_output *output) {
	// Workspace output priority
	for (int i = 0; i < root->outputs->length; i++) {
		struct wmiiv_output *other = root->outputs->items[i];
		if (other == output) {
			continue;
		}

		for (int j = 0; j < other->workspaces->length; j++) {
			struct wmiiv_workspace *workspace = other->workspaces->items[j];
			struct wmiiv_output *highest =
				workspace_output_get_highest_available(workspace, NULL);
			if (highest == output) {
				workspace_detach(workspace);
				output_add_workspace(output, workspace);
				ipc_event_workspace(NULL, workspace, "move");
				j--;
			}
		}

		if (other->workspaces->length == 0) {
			char *next = workspace_next_name(other->wlr_output->name);
			struct wmiiv_workspace *workspace = workspace_create(other, next);
			free(next);
			ipc_event_workspace(NULL, workspace, "init");
		}
	}

	// Saved workspaces
	while (root->fallback_output->workspaces->length) {
		struct wmiiv_workspace *workspace = root->fallback_output->workspaces->items[0];
		workspace_detach(workspace);
		output_add_workspace(output, workspace);

		// If the floater was made floating while on the NOOP output, its width
		// and height will be zero and it should be reinitialized as a floating
		// container to get the appropriate size and location. Additionally, if
		// the floater is wider or taller than the output or is completely
		// outside of the output's bounds, do the same as the output layout has
		// likely changed and the maximum size needs to be checked and the
		// floater re-centered
		for (int i = 0; i < workspace->floating->length; i++) {
			struct wmiiv_container *floater = workspace->floating->items[i];
			if (floater->pending.width == 0 || floater->pending.height == 0 ||
					floater->pending.width > output->width ||
					floater->pending.height > output->height ||
					floater->pending.x > output->lx + output->width ||
					floater->pending.y > output->ly + output->height ||
					floater->pending.x + floater->pending.width < output->lx ||
					floater->pending.y + floater->pending.height < output->ly) {
				container_floating_resize_and_center(floater);
			}
		}

		ipc_event_workspace(NULL, workspace, "move");
	}

	output_sort_workspaces(output);
}

struct wmiiv_output *output_create(struct wlr_output *wlr_output) {
	struct wmiiv_output *output = calloc(1, sizeof(struct wmiiv_output));
	node_init(&output->node, N_OUTPUT, output);
	output->wlr_output = wlr_output;
	wlr_output->data = output;
	output->detected_subpixel = wlr_output->subpixel;
	output->scale_filter = SCALE_FILTER_NEAREST;

	wl_signal_init(&output->events.disable);

	wl_list_insert(&root->all_outputs, &output->link);

	output->workspaces = create_list();
	output->current.workspaces = create_list();

	size_t len = sizeof(output->layers) / sizeof(output->layers[0]);
	for (size_t i = 0; i < len; ++i) {
		wl_list_init(&output->layers[i]);
	}

	return output;
}

void output_enable(struct wmiiv_output *output) {
	if (!wmiiv_assert(!output->enabled, "output is already enabled")) {
		return;
	}
	struct wlr_output *wlr_output = output->wlr_output;
	output->enabled = true;
	list_add(root->outputs, output);

	restore_workspaces(output);

	struct wmiiv_workspace *workspace = NULL;
	if (!output->workspaces->length) {
		// Create workspace
		char *workspace_name = workspace_next_name(wlr_output->name);
		wmiiv_log(WMIIV_DEBUG, "Creating default workspace %s", workspace_name);
		workspace = workspace_create(output, workspace_name);
		// Set each seat's focus if not already set
		struct wmiiv_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			if (!seat->has_focus) {
				seat_set_focus_workspace(seat, workspace);
			}
		}
		free(workspace_name);
		ipc_event_workspace(NULL, workspace, "init");
	}

	input_manager_configure_xcursor();

	wl_signal_emit(&root->events.new_node, &output->node);

	arrange_layers(output);
	arrange_root();
}

static void evacuate_sticky(struct wmiiv_workspace *old_workspace,
		struct wmiiv_output *new_output) {
	struct wmiiv_workspace *new_workspace = output_get_active_workspace(new_output);
	if (!wmiiv_assert(new_workspace, "New output does not have a workspace")) {
		return;
	}
	while(old_workspace->floating->length) {
		struct wmiiv_container *sticky = old_workspace->floating->items[0];
		window_detach(sticky);
		workspace_add_floating(new_workspace, sticky);
		container_handle_fullscreen_reparent(sticky);
		container_floating_move_to_center(sticky);
		ipc_event_window(sticky, "move");
	}
	workspace_detect_urgent(new_workspace);
}

static void output_evacuate(struct wmiiv_output *output) {
	if (!output->workspaces->length) {
		return;
	}
	struct wmiiv_output *fallback_output = NULL;
	if (root->outputs->length > 1) {
		fallback_output = root->outputs->items[0];
		if (fallback_output == output) {
			fallback_output = root->outputs->items[1];
		}
	}

	while (output->workspaces->length) {
		struct wmiiv_workspace *workspace = output->workspaces->items[0];

		workspace_detach(workspace);

		struct wmiiv_output *new_output =
			workspace_output_get_highest_available(workspace, output);
		if (!new_output) {
			new_output = fallback_output;
		}
		if (!new_output) {
			new_output = root->fallback_output;
		}

		struct wmiiv_workspace *new_output_workspace =
			output_get_active_workspace(new_output);

		if (workspace_is_empty(workspace)) {
			// If the new output has an active workspace (the noop output may
			// not have one), move all sticky containers to it
			if (new_output_workspace &&
					workspace_num_sticky_containers(workspace) > 0) {
				evacuate_sticky(workspace, new_output);
			}

			if (workspace_num_sticky_containers(workspace) == 0) {
				workspace_begin_destroy(workspace);
				continue;
			}
		}

		workspace_output_add_priority(workspace, new_output);
		output_add_workspace(new_output, workspace);
		output_sort_workspaces(new_output);
		ipc_event_workspace(NULL, workspace, "move");

		// If there is an old workspace (the noop output may not have one),
		// check to see if it is empty and should be destroyed.
		if (new_output_workspace) {
			workspace_consider_destroy(new_output_workspace);
		}
	}
}

void output_destroy(struct wmiiv_output *output) {
	if (!wmiiv_assert(output->node.destroying,
				"Tried to free output which wasn't marked as destroying")) {
		return;
	}
	if (!wmiiv_assert(output->wlr_output == NULL,
				"Tried to free output which still had a wlr_output")) {
		return;
	}
	if (!wmiiv_assert(output->node.ntxnrefs == 0, "Tried to free output "
				"which is still referenced by transactions")) {
		return;
	}
	list_free(output->workspaces);
	list_free(output->current.workspaces);
	wl_event_source_remove(output->repaint_timer);
	free(output);
}

static void untrack_output(struct wmiiv_container *container, void *data) {
	struct wmiiv_output *output = data;
	int index = list_find(container->outputs, output);
	if (index != -1) {
		list_del(container->outputs, index);
	}
}

void output_disable(struct wmiiv_output *output) {
	if (!wmiiv_assert(output->enabled, "Expected an enabled output")) {
		return;
	}
	int index = list_find(root->outputs, output);
	if (!wmiiv_assert(index >= 0, "Output not found in root node")) {
		return;
	}

	wmiiv_log(WMIIV_DEBUG, "Disabling output '%s'", output->wlr_output->name);
	wl_signal_emit(&output->events.disable, output);

	output_evacuate(output);

	root_for_each_container(untrack_output, output);

	list_del(root->outputs, index);

	output->enabled = false;
	output->current_mode = NULL;

	arrange_root();

	// Reconfigure all devices, since devices with map_to_output directives for
	// an output that goes offline should stop sending events as long as the
	// output remains offline.
	input_manager_configure_all_inputs();
}

void output_begin_destroy(struct wmiiv_output *output) {
	if (!wmiiv_assert(!output->enabled, "Expected a disabled output")) {
		return;
	}
	wmiiv_log(WMIIV_DEBUG, "Destroying output '%s'", output->wlr_output->name);
	wl_signal_emit(&output->node.events.destroy, &output->node);

	output->node.destroying = true;
	node_set_dirty(&output->node);
}

struct wmiiv_output *output_from_wlr_output(struct wlr_output *output) {
	return output->data;
}

struct wmiiv_output *output_get_in_direction(struct wmiiv_output *reference,
		enum wlr_direction direction) {
	if (!wmiiv_assert(direction, "got invalid direction: %d", direction)) {
		return NULL;
	}
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

void output_add_workspace(struct wmiiv_output *output,
		struct wmiiv_workspace *workspace) {
	if (workspace->output) {
		workspace_detach(workspace);
	}
	list_add(output->workspaces, workspace);
	workspace->output = output;
	node_set_dirty(&output->node);
	node_set_dirty(&workspace->node);
}

void output_for_each_workspace(struct wmiiv_output *output,
		void (*f)(struct wmiiv_workspace *workspace, void *data), void *data) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct wmiiv_workspace *workspace = output->workspaces->items[i];
		f(workspace, data);
	}
}

void output_for_each_container(struct wmiiv_output *output,
		void (*f)(struct wmiiv_container *container, void *data), void *data) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct wmiiv_workspace *workspace = output->workspaces->items[i];
		workspace_for_each_container(workspace, f, data);
	}
}

struct wmiiv_workspace *output_find_workspace(struct wmiiv_output *output,
		bool (*test)(struct wmiiv_workspace *workspace, void *data), void *data) {
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct wmiiv_workspace *workspace = output->workspaces->items[i];
		if (test(workspace, data)) {
			return workspace;
		}
	}
	return NULL;
}

struct wmiiv_container *output_find_container(struct wmiiv_output *output,
		bool (*test)(struct wmiiv_container *container, void *data), void *data) {
	struct wmiiv_container *result = NULL;
	for (int i = 0; i < output->workspaces->length; ++i) {
		struct wmiiv_workspace *workspace = output->workspaces->items[i];
		if ((result = workspace_find_container(workspace, test, data))) {
			return result;
		}
	}
	return NULL;
}

static int sort_workspace_cmp_qsort(const void *_a, const void *_b) {
	struct wmiiv_workspace *a = *(void **)_a;
	struct wmiiv_workspace *b = *(void **)_b;

	if (isdigit(a->name[0]) && isdigit(b->name[0])) {
		int a_num = strtol(a->name, NULL, 10);
		int b_num = strtol(b->name, NULL, 10);
		return (a_num < b_num) ? -1 : (a_num > b_num);
	} else if (isdigit(a->name[0])) {
		return -1;
	} else if (isdigit(b->name[0])) {
		return 1;
	}
	return 0;
}

void output_sort_workspaces(struct wmiiv_output *output) {
	list_stable_sort(output->workspaces, sort_workspace_cmp_qsort);
}

void output_get_box(struct wmiiv_output *output, struct wlr_box *box) {
	box->x = output->lx;
	box->y = output->ly;
	box->width = output->width;
	box->height = output->height;
}

enum wmiiv_container_layout output_get_default_layout(
		struct wmiiv_output *output) {
	return L_VERT;
}