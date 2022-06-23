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
#include "wmiiv/config.h"
#include "wmiiv/desktop.h"
#include "wmiiv/desktop/transaction.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/server.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/xdg_decoration.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

struct wmiiv_column *column_create(void) {
	struct wmiiv_column *c = calloc(1, sizeof(struct wmiiv_column));
	if (!c) {
		wmiiv_log(WMIIV_ERROR, "Unable to allocate wmiiv_column");
		return NULL;
	}
	node_init(&c->node, N_COLUMN, c);
	c->pending.layout = L_STACKED;
	c->alpha = 1.0f;

	c->pending.children = create_list();
	c->current.children = create_list();

	c->marks = create_list();
	c->outputs = create_list();

	wl_signal_init(&c->events.destroy);
	wl_signal_emit(&root->events.new_node, &c->node);

	return c;
}

void column_destroy(struct wmiiv_column *column) {
	if (!wmiiv_assert(column->node.destroying,
				"Tried to free column which wasn't marked as destroying")) {
		return;
	}
	if (!wmiiv_assert(column->node.ntxnrefs == 0, "Tried to free column "
				"which is still referenced by transactions")) {
		return;
	}
	free(column->title);
	free(column->formatted_title);
	wlr_texture_destroy(column->title_focused);
	wlr_texture_destroy(column->title_focused_inactive);
	wlr_texture_destroy(column->title_unfocused);
	wlr_texture_destroy(column->title_urgent);
	wlr_texture_destroy(column->title_focused_tab_title);
	list_free(column->pending.children);
	list_free(column->current.children);
	list_free(column->outputs);

	list_free_items_and_destroy(column->marks);
	wlr_texture_destroy(column->marks_focused);
	wlr_texture_destroy(column->marks_focused_inactive);
	wlr_texture_destroy(column->marks_unfocused);
	wlr_texture_destroy(column->marks_urgent);
	wlr_texture_destroy(column->marks_focused_tab_title);

	free(column);
}

void column_begin_destroy(struct wmiiv_column *column) {
	wl_signal_emit(&column->node.events.destroy, &column->node);

	column->node.destroying = true;
	node_set_dirty(&column->node);

	if (column->pending.workspace) {
		column_detach(column);
	}
}

void column_consider_destroy(struct wmiiv_column *column) {
	struct wmiiv_workspace *workspace = column->pending.workspace;

	if (column->pending.children->length) {
		return;
	}
	column_begin_destroy(column);

	if (workspace) {
		workspace_consider_destroy(workspace);
	}
}

void column_detach(struct wmiiv_column *column) {
	struct wmiiv_workspace *old_workspace = column->pending.workspace;

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
		workspace_update_representation(old_workspace);
		node_set_dirty(&old_workspace->node);
	}
	node_set_dirty(&column->node);
}

struct wmiiv_window *column_find_child(struct wmiiv_column *column,
		bool (*test)(struct wmiiv_window *container, void *data), void *data) {
	if (!column->pending.children) {
		return NULL;
	}
	for (int i = 0; i < column->pending.children->length; ++i) {
		struct wmiiv_window *child = column->pending.children->items[i];
		if (test(child, data)) {
			return child;
		}
	}
	return NULL;
}

void column_insert_child(struct wmiiv_column *parent,
		struct wmiiv_window *child, int i) {
	if (!wmiiv_assert(!child->pending.workspace && !child->pending.parent,
			"Windows must be detatched before they can be added to a column")) {
		window_detach(child);
	}
	list_insert(parent->pending.children, i, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	window_handle_fullscreen_reparent(child);
	column_update_representation(parent);
}

void column_add_sibling(struct wmiiv_window *fixed,
		struct wmiiv_window *active, bool after) {
	if (!wmiiv_assert(!active->pending.workspace && !active->pending.parent,
			"Windows must be detatched before they can be added to a column")) {
		window_detach(active);
	}

	list_t *siblings = window_get_siblings(fixed);
	int index = list_find(siblings, fixed);
	list_insert(siblings, index + after, active);
	active->pending.parent = fixed->pending.parent;
	active->pending.workspace = fixed->pending.workspace;
	window_handle_fullscreen_reparent(active);
	column_update_representation(fixed->pending.parent);
}

void column_add_child(struct wmiiv_column *parent,
		struct wmiiv_window *child) {
	if (!wmiiv_assert(!child->pending.workspace && !child->pending.workspace,
			"Windows must be detatched before they can be added to a column")) {
		window_detach(child);
	}
	list_add(parent->pending.children, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	window_handle_fullscreen_reparent(child);
	column_update_representation(parent);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}

void column_for_each_child(struct wmiiv_column *column,
		void (*f)(struct wmiiv_window *window, void *data),
		void *data) {
	if (column->pending.children)  {
		for (int i = 0; i < column->pending.children->length; ++i) {
			struct wmiiv_window *child = column->pending.children->items[i];
			f(child, data);
		}
	}
}

/**
 * Calculate and return the length of the tree representation.
 * An example tree representation is: V[Terminal, Firefox]
 * If buffer is not NULL, also populate the buffer with the representation.
 */
size_t column_build_representation(enum wmiiv_window_layout layout,
		list_t *children, char *buffer) {
	size_t len = 2;
	switch (layout) {
	case L_VERT:
		lenient_strcat(buffer, "V[");
		break;
	case L_HORIZ:
		lenient_strcat(buffer, "H[");
		break;
	case L_TABBED:
		lenient_strcat(buffer, "T[");
		break;
	case L_STACKED:
		lenient_strcat(buffer, "S[");
		break;
	case L_NONE:
		lenient_strcat(buffer, "D[");
		break;
	}
	for (int i = 0; i < children->length; ++i) {
		if (i != 0) {
			++len;
			lenient_strcat(buffer, " ");
		}
		struct wmiiv_window *child = children->items[i];
		const char *identifier = NULL;
		if (child->view) {
			identifier = view_get_class(child->view);
			if (!identifier) {
				identifier = view_get_app_id(child->view);
			}
		} else {
			identifier = child->formatted_title;
		}
		if (identifier) {
			len += strlen(identifier);
			lenient_strcat(buffer, identifier);
		} else {
			len += 6;
			lenient_strcat(buffer, "(null)");
		}
	}
	++len;
	lenient_strcat(buffer, "]");
	return len;
}

void column_update_representation(struct wmiiv_column *column) {
	size_t len = column_build_representation(column->pending.layout,
			column->pending.children, NULL);
	free(column->formatted_title);
	column->formatted_title = calloc(len + 1, sizeof(char));
	if (!wmiiv_assert(column->formatted_title,
				"Unable to allocate title string")) {
		return;
	}
	column_build_representation(column->pending.layout, column->pending.children,
			column->formatted_title);

	if (column->pending.workspace) {
		workspace_update_representation(column->pending.workspace);
	}
}

void column_get_box(struct wmiiv_column *column, struct wlr_box *box) {
	box->x = column->pending.x;
	box->y = column->pending.y;
	box->width = column->pending.width;
	box->height = column->pending.height;
}

/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void column_set_resizing(struct wmiiv_column *column, bool resizing) {
	if (!column) {
		return;
	}

	for (int i = 0; i < column->pending.children->length; ++i ) {
		struct wmiiv_window *child = column->pending.children->items[i];
		window_set_resizing(child, resizing);
	}
}

list_t *column_get_siblings(struct wmiiv_column *column) {
	if (column->pending.workspace) {
		return column->pending.workspace->tiling;
	}
	return NULL;
}

int column_sibling_index(struct wmiiv_column *child) {
	return list_find(column_get_siblings(child), child);
}

list_t *column_get_current_siblings(struct wmiiv_column *column) {
	if (column->current.workspace) {
		return column->current.workspace->tiling;
	}
	return NULL;
}

struct wmiiv_column *column_get_previous_sibling(struct wmiiv_column *column) {
	if (!wmiiv_assert(column->pending.workspace, "Column is not attached to a workspace")) {
		return NULL;
	}

	list_t *siblings = column->pending.workspace->tiling;
	int index = list_find(siblings, column);

	if (index <= 0) {
		return NULL;
	}

	return siblings->items[index - 1];
}

struct wmiiv_column *column_get_next_sibling(struct wmiiv_column *column) {
	if (!wmiiv_assert(column->pending.workspace, "Column is not attached to a workspace")) {
		return NULL;
	}

	list_t *siblings = column->pending.workspace->tiling;
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
struct wmiiv_output *column_get_effective_output(struct wmiiv_column *column) {
	if (column->outputs->length == 0) {
		return NULL;
	}
	return column->outputs->items[column->outputs->length - 1];
}

void column_discover_outputs(struct wmiiv_column *column) {
	struct wlr_box column_box = {
		.x = column->current.x,
		.y = column->current.y,
		.width = column->current.width,
		.height = column->current.height,
	};

	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		output_get_box(output, &output_box);
		struct wlr_box intersection;
		bool intersects =
			wlr_box_intersection(&intersection, &column_box, &output_box);
		int index = list_find(column->outputs, output);

		if (intersects && index == -1) {
			// Send enter
			wmiiv_log(WMIIV_DEBUG, "Container %p entered output %p", column, output);
			list_add(column->outputs, output);
		} else if (!intersects && index != -1) {
			// Send leave
			wmiiv_log(WMIIV_DEBUG, "Container %p left output %p", column, output);
			list_del(column->outputs, index);
		}
	}
}

static bool find_urgent_iterator(struct wmiiv_window *container, void *data) {
	return container->view && view_is_urgent(container->view);
}

bool column_has_urgent_child(struct wmiiv_column *column) {
	return column_find_child(column, find_urgent_iterator, NULL);
}

