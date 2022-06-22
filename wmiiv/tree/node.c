#define _POSIX_C_SOURCE 200809L
#include "wmiiv/output.h"
#include "wmiiv/server.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/node.h"
#include "wmiiv/tree/root.h"
#include "wmiiv/tree/workspace.h"
#include "log.h"

void node_init(struct wmiiv_node *node, enum wmiiv_node_type type, void *thing) {
	static size_t next_id = 1;
	node->id = next_id++;
	node->type = type;
	node->wmiiv_root = thing;
	wl_signal_init(&node->events.destroy);
}

const char *node_type_to_str(enum wmiiv_node_type type) {
	switch (type) {
	case N_ROOT:
		return "N_ROOT";
	case N_OUTPUT:
		return "N_OUTPUT";
	case N_WORKSPACE:
		return "N_WORKSPACE";
	case N_COLUMN:
		return "N_COLUMN";
	case N_WINDOW:
		return "N_WINDOW";
	}
	return "";
}

void node_set_dirty(struct wmiiv_node *node) {
	if (node->dirty) {
		return;
	}
	node->dirty = true;
	list_add(server.dirty_nodes, node);
}

// TODO (wmiiv) rename to node_is_window.
bool node_is_view(struct wmiiv_node *node) {
	return node->type == N_WINDOW;
}

char *node_get_name(struct wmiiv_node *node) {
	switch (node->type) {
	case N_ROOT:
		return "root";
	case N_OUTPUT:
		return node->wmiiv_output->wlr_output->name;
	case N_WORKSPACE:
		return node->wmiiv_workspace->name;
	case N_COLUMN:
		return node->wmiiv_column->title;
	case N_WINDOW:
		return node->wmiiv_window->title;
	}
	return NULL;
}

void node_get_box(struct wmiiv_node *node, struct wlr_box *box) {
	switch (node->type) {
	case N_ROOT:
		root_get_box(root, box);
		break;
	case N_OUTPUT:
		output_get_box(node->wmiiv_output, box);
		break;
	case N_WORKSPACE:
		workspace_get_box(node->wmiiv_workspace, box);
		break;
	case N_COLUMN:
		column_get_box(node->wmiiv_column, box);
		break;
	case N_WINDOW:
		window_get_box(node->wmiiv_window, box);
		break;
	}
}

struct wmiiv_output *node_get_output(struct wmiiv_node *node) {
	switch (node->type) {
	case N_WORKSPACE:
		return node->wmiiv_workspace->output;
	case N_OUTPUT:
		return node->wmiiv_output;
	case N_ROOT:
		return NULL;
	case N_COLUMN: {
			struct wmiiv_workspace *workspace = node->wmiiv_column->pending.workspace;
			return workspace ? workspace->output : NULL;
		}
	case N_WINDOW: {
			struct wmiiv_workspace *workspace = node->wmiiv_window->pending.workspace;
			return workspace ? workspace->output : NULL;
		}	
	}
	return NULL;
}

enum wmiiv_container_layout node_get_layout(struct wmiiv_node *node) {
	switch (node->type) {
	case N_ROOT:
		return L_NONE;
	case N_OUTPUT:
		return L_NONE;
	case N_WORKSPACE:
		return L_HORIZ;
	case N_COLUMN:
		return node->wmiiv_column->pending.layout;
	case N_WINDOW:
		return L_NONE;
	}
	return L_NONE;
}

struct wmiiv_node *node_get_parent(struct wmiiv_node *node) {
	switch (node->type) {
	case N_ROOT:
		return NULL;
	case N_OUTPUT:
		return &root->node;
	case N_WORKSPACE: {
			struct wmiiv_workspace *workspace = node->wmiiv_workspace;
			if (workspace->output) {
				return &workspace->output->node;
			}
		}
		return NULL;
	case N_COLUMN: {
			struct wmiiv_column *column = node->wmiiv_column;
			if (column->pending.parent) {
				return &column->pending.parent->node;
			}
			if (column->pending.workspace) {
				return &column->pending.workspace->node;
			}
		}
		return NULL;
	case N_WINDOW: {
			struct wmiiv_container *window = node->wmiiv_window;
			if (window->pending.parent) {
				return &window->pending.parent->node;
			}
			if (window->pending.workspace) {
				return &window->pending.workspace->node;
			}
		}
		return NULL;
	}
	return NULL;
}

list_t *node_get_children(struct wmiiv_node *node) {
	switch (node->type) {
	case N_ROOT:
		return NULL;
	case N_OUTPUT:
		return NULL;
	case N_WORKSPACE:
		return node->wmiiv_workspace->tiling;
	case N_COLUMN:
		return node->wmiiv_column->pending.children;
	case N_WINDOW:
		return NULL;
	}
	return NULL;
}

bool node_has_ancestor(struct wmiiv_node *node, struct wmiiv_node *ancestor) {
	if (ancestor->type == N_ROOT && node->type == N_WINDOW &&
			node->wmiiv_window->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		return true;
	}
	struct wmiiv_node *parent = node_get_parent(node);
	while (parent) {
		if (parent == ancestor) {
			return true;
		}
		parent = node_get_parent(parent);
	}
	return false;
}
