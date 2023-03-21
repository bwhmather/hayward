#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/node.h"

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

void
node_init(struct hayward_node *node, enum hayward_node_type type, void *thing) {
    static size_t next_id = 1;
    node->id = next_id++;
    node->type = type;
    node->hayward_root = thing;
    wl_signal_init(&node->events.destroy);
}

const char *
node_type_to_str(enum hayward_node_type type) {
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

void
node_set_dirty(struct hayward_node *node) {
    hayward_assert(node->type != N_WINDOW, "Use window_set_dirty");
    if (node->dirty) {
        return;
    }
    node->dirty = true;
    list_add(server.dirty_nodes, node);
}

// TODO (hayward) rename to node_is_window.
bool
node_is_view(struct hayward_node *node) {
    return node->type == N_WINDOW;
}

char *
node_get_name(struct hayward_node *node) {
    switch (node->type) {
    case N_ROOT:
        return "root";
    case N_OUTPUT:
        return node->hayward_output->wlr_output->name;
    case N_WORKSPACE:
        return node->hayward_workspace->name;
    case N_COLUMN:
        return "column";
    case N_WINDOW:
        return node->hayward_window->title;
    }
    return NULL;
}

void
node_get_box(struct hayward_node *node, struct wlr_box *box) {
    switch (node->type) {
    case N_ROOT:
        root_get_box(root, box);
        break;
    case N_OUTPUT:
        output_get_box(node->hayward_output, box);
        break;
    case N_WORKSPACE:
        workspace_get_box(node->hayward_workspace, box);
        break;
    case N_COLUMN:
        column_get_box(node->hayward_column, box);
        break;
    case N_WINDOW:
        window_get_box(node->hayward_window, box);
        break;
    }
}

struct hayward_node *
node_get_parent(struct hayward_node *node) {
    switch (node->type) {
    case N_ROOT:
        return NULL;
    case N_OUTPUT:
        return &root->node;
    case N_WORKSPACE:
        return &root->node;
    case N_COLUMN: {
        struct hayward_column *column = node->hayward_column;
        if (column->pending.workspace) {
            return &column->pending.workspace->node;
        }
    }
        return NULL;
    case N_WINDOW: {
        struct hayward_window *window = node->hayward_window;
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

list_t *
node_get_children(struct hayward_node *node) {
    switch (node->type) {
    case N_ROOT:
        return NULL;
    case N_OUTPUT:
        return NULL;
    case N_WORKSPACE:
        return node->hayward_workspace->pending.tiling;
    case N_COLUMN:
        return node->hayward_column->pending.children;
    case N_WINDOW:
        return NULL;
    }
    return NULL;
}

bool
node_has_ancestor(struct hayward_node *node, struct hayward_node *ancestor) {
    struct hayward_node *parent = node_get_parent(node);
    while (parent) {
        if (parent == ancestor) {
            return true;
        }
        parent = node_get_parent(parent);
    }
    return false;
}
