#ifndef _HAYWARD_COLUMN_H
#define _HAYWARD_COLUMN_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "hayward-common/list.h"
#include "hayward/tree/node.h"

enum hayward_column_layout {
	L_SPLIT,
	L_STACKED,
};

struct hayward_column_state {
	// Container properties
	enum hayward_column_layout layout;
	double x, y;
	double width, height;

	// Cached backlink to containing workspace.
	struct hayward_workspace *workspace;

	// Backling to output.  This is actually the golden source, but should
	// always be updated using the reconciliation functions.
	struct hayward_output *output;

	// Cached flag indicating whether the column contains the focused
	// window.  Should only be updated using the reconciliation functions.
	bool focused;

	list_t *children;                 // struct hayward_window

	struct hayward_window *active_child;
};

struct hayward_column {
	struct hayward_node node;

	struct hayward_column_state current;
	struct hayward_column_state pending;

	// For C_ROOT, this has no meaning
	// For other types, this is the position in layout coordinates
	// Includes borders
	double saved_x, saved_y;
	double saved_width, saved_height;

	// The share of the space of parent workspace this container occupies.
	double width_fraction;

	// The share of space of the parent container that all children occupy
	// Used for doing the resize calculations
	double child_total_width;
	double child_total_height;

	// Outputs currently being intersected
	list_t *outputs; // struct hayward_output

	float alpha;

	struct {
		struct wl_signal destroy;
	} events;
};


struct hayward_column *column_create(void);

void column_destroy(struct hayward_column *column);

void column_begin_destroy(struct hayward_column *column);

void column_consider_destroy(struct hayward_column *container);

void column_detach(struct hayward_column *column);

void column_reconcile(struct hayward_column *column, struct hayward_workspace *workspace, struct hayward_output *output);
void column_reconcile_detached(struct hayward_column *column);


/**
 * Search a container's descendants a container based on test criteria. Returns
 * the first container that passes the test.
 */
struct hayward_window *column_find_child(struct hayward_column *container,
		bool (*test)(struct hayward_window *view, void *data), void *data);

void column_add_child(struct hayward_column *parent,
		struct hayward_window *child);

void column_insert_child(struct hayward_column *parent,
		struct hayward_window *child, int i);

void column_remove_child(struct hayward_column *parent, struct hayward_window *child);

void column_set_active_child(struct hayward_column *column, struct hayward_window *window);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void column_add_sibling(struct hayward_window *parent,
		struct hayward_window *child, bool after);


void column_for_each_child(struct hayward_column *column,
		void (*f)(struct hayward_window *window, void *data), void *data);

/**
 * Get a column's box in layout coordinates.
 */
void column_get_box(struct hayward_column *column, struct wlr_box *box);

void column_set_resizing(struct hayward_column *column, bool resizing);

list_t *column_get_siblings(struct hayward_column *column);

int column_sibling_index(struct hayward_column *child);

list_t *column_get_current_siblings(struct hayward_column *column);

struct hayward_column *column_get_previous_sibling(struct hayward_column *column);
struct hayward_column *column_get_next_sibling(struct hayward_column *column);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 * If the container is not on any output, return NULL.
 */
struct hayward_output *column_get_effective_output(struct hayward_column *column);

void column_discover_outputs(struct hayward_column *column);

bool column_has_urgent_child(struct hayward_column *column);

#endif
