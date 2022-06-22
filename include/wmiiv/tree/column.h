#ifndef _WMIIV_COLUMN_H
#define _WMIIV_COLUMN_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "wmiiv/tree/node.h"

struct wmiiv_column_state {
	// Container properties
	enum wmiiv_window_layout layout;
	double x, y;
	double width, height;

	enum wmiiv_fullscreen_mode fullscreen_mode;

	struct wmiiv_workspace *workspace;
	struct wmiiv_column *parent;    // NULL if container in root of workspace
	list_t *children;                 // struct wmiiv_window

	struct wmiiv_window *focused_inactive_child;
	bool focused;

	enum wmiiv_window_border border;
	int border_thickness;
	bool border_top;
	bool border_bottom;
	bool border_left;
	bool border_right;

	// These are in layout coordinates.
	double content_x, content_y;
	double content_width, content_height;
};

struct wmiiv_column {
	struct wmiiv_node node;
	struct wmiiv_view *view;

	struct wmiiv_column_state current;
	struct wmiiv_column_state pending;

	char *title;           // The view's title (unformatted)
	char *formatted_title; // The title displayed in the title bar

	// Whether stickiness has been enabled on this container. Use
	// `container_is_sticky_[or_child]` rather than accessing this field
	// directly; it'll also check that the container is floating.
	bool is_sticky;

	// For C_ROOT, this has no meaning
	// For other types, this is the position in layout coordinates
	// Includes borders
	double saved_x, saved_y;
	double saved_width, saved_height;

	// Used when the view changes to CSD unexpectedly. This will be a non-B_CSD
	// border which we use to restore when the view returns to SSD.
	enum wmiiv_window_border saved_border;

	// The share of the space of parent container this container occupies
	double width_fraction;
	double height_fraction;

	// The share of space of the parent container that all children occupy
	// Used for doing the resize calculations
	double child_total_width;
	double child_total_height;

	// In most cases this is the same as the content x and y, but if the view
	// refuses to resize to the content dimensions then it can be smaller.
	// These are in layout coordinates.
	double surface_x, surface_y;

	// Outputs currently being intersected
	list_t *outputs; // struct wmiiv_output

	float alpha;

	struct wlr_texture *title_focused;
	struct wlr_texture *title_focused_inactive;
	struct wlr_texture *title_focused_tab_title;
	struct wlr_texture *title_unfocused;
	struct wlr_texture *title_urgent;

	list_t *marks; // char *
	struct wlr_texture *marks_focused;
	struct wlr_texture *marks_focused_inactive;
	struct wlr_texture *marks_focused_tab_title;
	struct wlr_texture *marks_unfocused;
	struct wlr_texture *marks_urgent;

	struct {
		struct wl_signal destroy;
	} events;
};


struct wmiiv_column *column_create(void);

void column_destroy(struct wmiiv_column *column);

void column_begin_destroy(struct wmiiv_column *column);

void column_consider_destroy(struct wmiiv_column *container);

/**
 * Search a container's descendants a container based on test criteria. Returns
 * the first container that passes the test.
 */
struct wmiiv_window *column_find_child(struct wmiiv_column *container,
		bool (*test)(struct wmiiv_window *view, void *data), void *data);

void column_add_child(struct wmiiv_column *parent,
		struct wmiiv_window *child);

void column_insert_child(struct wmiiv_column *parent,
		struct wmiiv_window *child, int i);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void column_add_sibling(struct wmiiv_window *parent,
		struct wmiiv_window *child, bool after);

void column_detach(struct wmiiv_column *column);

void column_for_each_child(struct wmiiv_column *column,
		void (*f)(struct wmiiv_window *window, void *data), void *data);

size_t column_build_representation(enum wmiiv_window_layout layout,
		list_t *children, char *buffer);

void column_update_representation(struct wmiiv_column *column);

/**
 * Get a column's box in layout coordinates.
 */
void column_get_box(struct wmiiv_column *column, struct wlr_box *box);

void column_set_resizing(struct wmiiv_column *column, bool resizing);

list_t *column_get_siblings(struct wmiiv_column *column);

int column_sibling_index(struct wmiiv_column *child);

list_t *column_get_current_siblings(struct wmiiv_column *column);

struct wmiiv_column *column_get_previous_sibling(struct wmiiv_column *column);
struct wmiiv_column *column_get_next_sibling(struct wmiiv_column *column);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 * If the container is not on any output, return NULL.
 */
struct wmiiv_output *column_get_effective_output(struct wmiiv_column *column);

void column_discover_outputs(struct wmiiv_column *column);

bool column_has_urgent_child(struct wmiiv_column *column);

#endif
