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

struct wmiiv_container *column_create(void) {
	struct wmiiv_container *c = calloc(1, sizeof(struct wmiiv_container));
	if (!c) {
		wmiiv_log(WMIIV_ERROR, "Unable to allocate wmiiv_container");
		return NULL;
	}
	node_init(&c->node, N_COLUMN, c);
	c->pending.layout = L_STACKED;
	c->view = NULL;
	c->alpha = 1.0f;

	c->pending.children = create_list();
	c->current.children = create_list();

	c->marks = create_list();
	c->outputs = create_list();

	wl_signal_init(&c->events.destroy);
	wl_signal_emit(&root->events.new_node, &c->node);

	return c;
}

void column_consider_destroy(struct wmiiv_container *column) {
	if (!wmiiv_assert(container_is_column(column), "Cannot reap a non-column container")) {
		return;
	}
	struct wmiiv_workspace *workspace = column->pending.workspace;

	if (column->pending.children->length) {
		return;
	}
	container_begin_destroy(column);

	if (workspace) {
		workspace_consider_destroy(workspace);
	}
}

void column_detach(struct wmiiv_container *column) {
	struct wmiiv_workspace *old_workspace = column->pending.workspace;

	list_t *siblings = container_get_siblings(column);
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

struct wmiiv_container *column_find_child(struct wmiiv_container *column,
		bool (*test)(struct wmiiv_container *container, void *data), void *data) {
	if (!wmiiv_assert(container_is_column(column), "Cannot find children in non-column containers")) {
		return NULL;
	}
	if (!column->pending.children) {
		return NULL;
	}
	for (int i = 0; i < column->pending.children->length; ++i) {
		struct wmiiv_container *child = column->pending.children->items[i];
		if (test(child, data)) {
			return child;
		}
	}
	return NULL;
}

void column_insert_child(struct wmiiv_container *parent,
		struct wmiiv_container *child, int i) {
	wmiiv_assert(container_is_column(parent), "Target is not a column");
	wmiiv_assert(container_is_window(child), "Not a window");

	if (!wmiiv_assert(!child->pending.workspace && !child->pending.parent,
			"Windows must be detatched before they can be added to a column")) {
		window_detach(child);
	}
	list_insert(parent->pending.children, i, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_handle_fullscreen_reparent(child);
	column_update_representation(parent);
}

void column_add_sibling(struct wmiiv_container *fixed,
		struct wmiiv_container *active, bool after) {
	wmiiv_assert(container_is_window(fixed), "Target sibling is not a window");
	wmiiv_assert(container_is_window(active), "Not a window");

	if (!wmiiv_assert(!active->pending.workspace && !active->pending.parent,
			"Windows must be detatched before they can be added to a column")) {
		window_detach(active);
	}

	list_t *siblings = container_get_siblings(fixed);
	int index = list_find(siblings, fixed);
	list_insert(siblings, index + after, active);
	active->pending.parent = fixed->pending.parent;
	active->pending.workspace = fixed->pending.workspace;
	container_handle_fullscreen_reparent(active);
	column_update_representation(fixed->pending.parent);
}

void column_add_child(struct wmiiv_container *parent,
		struct wmiiv_container *child) {
	wmiiv_assert(container_is_column(parent), "Target is not a column");
	wmiiv_assert(container_is_window(child), "Not a window");

	if (!wmiiv_assert(!child->pending.workspace && !child->pending.workspace,
			"Windows must be detatched before they can be added to a column")) {
		window_detach(child);
	}
	list_add(parent->pending.children, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_handle_fullscreen_reparent(child);
	column_update_representation(parent);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}

void column_for_each_child(struct wmiiv_container *column,
		void (*f)(struct wmiiv_container *window, void *data),
		void *data) {
	wmiiv_assert(container_is_column(column), "Expected column");

	if (column->pending.children)  {
		for (int i = 0; i < column->pending.children->length; ++i) {
			struct wmiiv_container *child = column->pending.children->items[i];
			f(child, data);
		}
	}
}

void column_damage_whole(struct wmiiv_container *column) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_whole_container(output, column);
	}
}

/**
 * Calculate and return the length of the tree representation.
 * An example tree representation is: V[Terminal, Firefox]
 * If buffer is not NULL, also populate the buffer with the representation.
 */
size_t column_build_representation(enum wmiiv_container_layout layout,
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
		struct wmiiv_container *child = children->items[i];
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

void column_update_representation(struct wmiiv_container *column) {
	wmiiv_assert(container_is_column(column), "Expected column");

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

/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void column_set_resizing(struct wmiiv_container *column, bool resizing) {
	if (!column) {
		return;
	}

	wmiiv_assert(container_is_column(column), "Expected column");

	for (int i = 0; i < column->pending.children->length; ++i ) {
		struct wmiiv_container *child = column->pending.children->items[i];
		window_set_resizing(child, resizing);
	}
}
