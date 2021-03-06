#ifndef _HAYWARD_NODE_H
#define _HAYWARD_NODE_H
#include <stdbool.h>
#include "list.h"

#define MIN_SANE_W 100
#define MIN_SANE_H 60

struct hayward_root;
struct hayward_output;
struct hayward_workspace;
struct hayward_window;
struct hayward_transaction_instruction;
struct wlr_box;

enum hayward_node_type {
	N_ROOT,
	N_OUTPUT,
	N_WORKSPACE,
	N_COLUMN,
	N_WINDOW,
};

struct hayward_node {
	enum hayward_node_type type;
	union {
		struct hayward_root *hayward_root;
		struct hayward_output *hayward_output;
		struct hayward_workspace *hayward_workspace;
		struct hayward_column *hayward_column;
		struct hayward_window *hayward_window;
	};

	/**
	 * A unique ID to identify this node.
	 * Primarily used in the get_tree JSON output.
	 */
	size_t id;

	struct hayward_transaction_instruction *instruction;
	size_t ntxnrefs;
	bool destroying;

	// If true, indicates that the container has pending state that differs from
	// the current.
	bool dirty;

	struct {
		struct wl_signal destroy;
	} events;
};

void node_init(struct hayward_node *node, enum hayward_node_type type, void *thing);

const char *node_type_to_str(enum hayward_node_type type);

/**
 * Mark a node as dirty if it isn't already. Dirty nodes will be included in the
 * next transaction then unmarked as dirty.
 */
void node_set_dirty(struct hayward_node *node);

bool node_is_view(struct hayward_node *node);

char *node_get_name(struct hayward_node *node);

void node_get_box(struct hayward_node *node, struct wlr_box *box);

struct hayward_node *node_get_parent(struct hayward_node *node);

list_t *node_get_children(struct hayward_node *node);

bool node_has_ancestor(struct hayward_node *node, struct hayward_node *ancestor);

#endif
