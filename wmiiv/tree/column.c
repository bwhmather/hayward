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
		wmiiv_log(SWAY_ERROR, "Unable to allocate wmiiv_container");
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

void column_consider_destroy(struct wmiiv_container *col) {
	if (!wmiiv_assert(container_is_column(col), "Cannot reap a non-column container")) {
		return;
	}
	struct wmiiv_workspace *ws = col->pending.workspace;

	if (col->pending.children->length) {
		return;
	}
	container_begin_destroy(col);

	if (ws) {
		workspace_consider_destroy(ws);
	}
}

struct wmiiv_container *column_find_child(struct wmiiv_container *col,
		bool (*test)(struct wmiiv_container *con, void *data), void *data) {
	if (!wmiiv_assert(container_is_column(col), "Cannot find children in non-column containers")) {
		return NULL;
	}
	if (!col->pending.children) {
		return NULL;
	}
	for (int i = 0; i < col->pending.children->length; ++i) {
		struct wmiiv_container *child = col->pending.children->items[i];
		if (test(child, data)) {
			return child;
		}
	}
	return NULL;
}

static void set_workspace(struct wmiiv_container *container, void *data) {
	container->pending.workspace = container->pending.parent->pending.workspace;
}

void column_insert_child(struct wmiiv_container *parent,
		struct wmiiv_container *child, int i) {
	wmiiv_assert(container_is_column(parent), "Target is not a column");
	wmiiv_assert(container_is_window(child), "Not a window");

	if (!wmiiv_assert(!child->pending.workspace && !child->pending.parent,
			"Windows must be detatched before they can be added to a column")) {
		container_detach(child);
	}
	list_insert(parent->pending.children, i, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_for_each_child(child, set_workspace, NULL);
	container_handle_fullscreen_reparent(child);
	container_update_representation(parent);
}

void column_add_sibling(struct wmiiv_container *fixed,
		struct wmiiv_container *active, bool after) {
	wmiiv_assert(container_is_window(fixed), "Target sibling is not a window");
	wmiiv_assert(container_is_window(active), "Not a window");

	if (!wmiiv_assert(!active->pending.workspace && !active->pending.parent,
			"Windows must be detatched before they can be added to a column")) {
		container_detach(active);
	}

	list_t *siblings = container_get_siblings(fixed);
	int index = list_find(siblings, fixed);
	list_insert(siblings, index + after, active);
	active->pending.parent = fixed->pending.parent;
	active->pending.workspace = fixed->pending.workspace;
	container_for_each_child(active, set_workspace, NULL);
	container_handle_fullscreen_reparent(active);
	container_update_representation(active);
}

void column_add_child(struct wmiiv_container *parent,
		struct wmiiv_container *child) {
	wmiiv_assert(container_is_column(parent), "Target is not a column");
	wmiiv_assert(container_is_window(child), "Not a window");

	if (!wmiiv_assert(!child->pending.workspace && !child->pending.workspace,
			"Windows must be detatched before they can be added to a column")) {
		container_detach(child);
	}
	list_add(parent->pending.children, child);
	child->pending.parent = parent;
	child->pending.workspace = parent->pending.workspace;
	container_for_each_child(child, set_workspace, NULL);
	container_handle_fullscreen_reparent(child);
	container_update_representation(parent);
	node_set_dirty(&child->node);
	node_set_dirty(&parent->node);
}
