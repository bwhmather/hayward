#ifndef _WMIIV_NODE_H
#define _WMIIV_NODE_H
#include <stdbool.h>
#include "list.h"

#define MIN_SANE_W 100
#define MIN_SANE_H 60

struct wmiiv_root;
struct wmiiv_output;
struct wmiiv_workspace;
struct wmiiv_window;
struct wmiiv_transaction_instruction;
struct wlr_box;

enum wmiiv_node_type {
	N_ROOT,
	N_OUTPUT,
	N_WORKSPACE,
	N_COLUMN,
	N_WINDOW,
};

struct wmiiv_node {
	enum wmiiv_node_type type;
	union {
		struct wmiiv_root *wmiiv_root;
		struct wmiiv_output *wmiiv_output;
		struct wmiiv_workspace *wmiiv_workspace;
		struct wmiiv_column *wmiiv_column;
		struct wmiiv_window *wmiiv_window;
	};

	/**
	 * A unique ID to identify this node.
	 * Primarily used in the get_tree JSON output.
	 */
	size_t id;

	struct wmiiv_transaction_instruction *instruction;
	size_t ntxnrefs;
	bool destroying;

	// If true, indicates that the container has pending state that differs from
	// the current.
	bool dirty;

	struct {
		struct wl_signal destroy;
	} events;
};

void node_init(struct wmiiv_node *node, enum wmiiv_node_type type, void *thing);

const char *node_type_to_str(enum wmiiv_node_type type);

/**
 * Mark a node as dirty if it isn't already. Dirty nodes will be included in the
 * next transaction then unmarked as dirty.
 */
void node_set_dirty(struct wmiiv_node *node);

bool node_is_view(struct wmiiv_node *node);

char *node_get_name(struct wmiiv_node *node);

void node_get_box(struct wmiiv_node *node, struct wlr_box *box);

struct wmiiv_output *node_get_output(struct wmiiv_node *node);

struct wmiiv_node *node_get_parent(struct wmiiv_node *node);

list_t *node_get_children(struct wmiiv_node *node);

bool node_has_ancestor(struct wmiiv_node *node, struct wmiiv_node *ancestor);

#endif
