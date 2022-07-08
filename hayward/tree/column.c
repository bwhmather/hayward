#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/render/drm_format_set.h>
#include "linux-dmabuf-unstable-v1-protocol.h"
#include "cairo_util.h"
#include "pango.h"
#include "hayward/config.h"
#include "hayward/desktop.h"
#include "hayward/desktop/transaction.h"
#include "hayward/input/input-manager.h"
#include "hayward/input/seat.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/server.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "hayward/xdg_decoration.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

struct hayward_column *column_create(void) {
	struct hayward_column *c = calloc(1, sizeof(struct hayward_column));
	if (!c) {
		hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_column");
		return NULL;
	}
	node_init(&c->node, N_COLUMN, c);
	c->pending.layout = L_STACKED;
	c->alpha = 1.0f;

	c->pending.children = create_list();
	c->current.children = create_list();

	c->outputs = create_list();

	wl_signal_init(&c->events.destroy);
	wl_signal_emit(&root->events.new_node, &c->node);

	return c;
}

void column_destroy(struct hayward_column *column) {
	hayward_assert(column->node.destroying,
				"Tried to free column which wasn't marked as destroying");
	hayward_assert(column->node.ntxnrefs == 0, "Tried to free column "
				"which is still referenced by transactions");
	list_free(column->pending.children);
	list_free(column->current.children);
	list_free(column->outputs);

	free(column);
}

void column_begin_destroy(struct hayward_column *column) {
	wl_signal_emit(&column->node.events.destroy, &column->node);

	column->node.destroying = true;
	node_set_dirty(&column->node);

	if (column->pending.workspace) {
		column_detach(column);
	}
}

void column_consider_destroy(struct hayward_column *column) {
	struct hayward_workspace *workspace = column->pending.workspace;

	if (column->pending.children->length) {
		return;
	}
	column_begin_destroy(column);

	if (workspace) {
		workspace_consider_destroy(workspace);
	}
}

void column_detach(struct hayward_column *column) {
	struct hayward_workspace *old_workspace = column->pending.workspace;

	list_t *siblings = column_get_siblings(column);
	if (siblings) {
		int index = list_find(siblings, column);
		if (index != -1) {
			list_del(siblings, index);
		}
	}
	column->pending.parent = NULL;
	column->pending.workspace = NULL;

	if (old_workspace) {
		node_set_dirty(&old_workspace->node);
	}
	node_set_dirty(&column->node);
}

struct hayward_window *column_find_child(struct hayward_column *column,
		bool (*test)(struct hayward_window *container, void *data), void *data) {
	if (!column->pending.children) {
		return NULL;
	}
	for (int i = 0; i < column->pending.children->length; ++i) {
		struct hayward_window *child = column->pending.children->items[i];
		if (test(child, data)) {
			return child;
		}
	}
	return NULL;
}

void column_insert_child(struct hayward_column *parent,
		struct hayward_window *child, int i) {
	hayward_assert(!child->pending.workspace && !child->pending.parent,
			"Windows must be detatched before they can be added to a column");
	list_insert(parent->pending.children, i, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	window_handle_fullscreen_reparent(child);
}

void column_add_sibling(struct hayward_window *fixed,
		struct hayward_window *active, bool after) {
	hayward_assert(!active->pending.workspace && !active->pending.parent,
			"Windows must be detatched before they can be added to a column");

	list_t *siblings = window_get_siblings(fixed);
	int index = list_find(siblings, fixed);
	list_insert(siblings, index + after, active);
	active->pending.parent = fixed->pending.parent;
	active->pending.workspace = fixed->pending.workspace;
	window_handle_fullscreen_reparent(active);
}

void column_add_child(struct hayward_column *parent,
		struct hayward_window *child) {
	hayward_assert(!child->pending.workspace && !child->pending.workspace,
			"Windows must be detatched before they can be added to a column");
	list_add(parent->pending.children, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	window_handle_fullscreen_reparent(child);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}

void column_for_each_child(struct hayward_column *column,
		void (*f)(struct hayward_window *window, void *data),
		void *data) {
	if (column->pending.children)  {
		for (int i = 0; i < column->pending.children->length; ++i) {
			struct hayward_window *child = column->pending.children->items[i];
			f(child, data);
		}
	}
}

void column_get_box(struct hayward_column *column, struct wlr_box *box) {
	box->x = column->pending.x;
	box->y = column->pending.y;
	box->width = column->pending.width;
	box->height = column->pending.height;
}

/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void column_set_resizing(struct hayward_column *column, bool resizing) {
	if (!column) {
		return;
	}

	for (int i = 0; i < column->pending.children->length; ++i ) {
		struct hayward_window *child = column->pending.children->items[i];
		window_set_resizing(child, resizing);
	}
}

list_t *column_get_siblings(struct hayward_column *column) {
	if (column->pending.workspace) {
		return column->pending.workspace->pending.tiling;
	}
	return NULL;
}

int column_sibling_index(struct hayward_column *child) {
	return list_find(column_get_siblings(child), child);
}

list_t *column_get_current_siblings(struct hayward_column *column) {
	if (column->current.workspace) {
		return column->current.workspace->pending.tiling;
	}
	return NULL;
}

struct hayward_column *column_get_previous_sibling(struct hayward_column *column) {
	hayward_assert(column->pending.workspace, "Column is not attached to a workspace");

	list_t *siblings = column->pending.workspace->pending.tiling;
	int index = list_find(siblings, column);

	if (index <= 0) {
		return NULL;
	}

	return siblings->items[index - 1];
}

struct hayward_column *column_get_next_sibling(struct hayward_column *column) {
	hayward_assert(column->pending.workspace, "Column is not attached to a workspace");

	list_t *siblings = column->pending.workspace->pending.tiling;
	int index = list_find(siblings, column);

	if (index < 0) {
		return NULL;
	}

	if (index >= siblings->length - 1) {
		return NULL;
	}

	return siblings->items[index + 1];
}

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct hayward_output *column_get_effective_output(struct hayward_column *column) {
	if (column->outputs->length == 0) {
		return NULL;
	}
	return column->outputs->items[column->outputs->length - 1];
}

void column_discover_outputs(struct hayward_column *column) {
	struct wlr_box column_box = {
		.x = column->current.x,
		.y = column->current.y,
		.width = column->current.width,
		.height = column->current.height,
	};

	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		output_get_box(output, &output_box);
		struct wlr_box intersection;
		bool intersects =
			wlr_box_intersection(&intersection, &column_box, &output_box);
		int index = list_find(column->outputs, output);

		if (intersects && index == -1) {
			// Send enter
			hayward_log(HAYWARD_DEBUG, "Container %p entered output %p", (void *) column, (void *) output);
			list_add(column->outputs, output);
		} else if (!intersects && index != -1) {
			// Send leave
			hayward_log(HAYWARD_DEBUG, "Container %p left output %p", (void *) column, (void *) output);
			list_del(column->outputs, index);
		}
	}
}

static bool find_urgent_iterator(struct hayward_window *container, void *data) {
	return container->view && view_is_urgent(container->view);
}

bool column_has_urgent_child(struct hayward_column *column) {
	return column_find_child(column, find_urgent_iterator, NULL);
}

