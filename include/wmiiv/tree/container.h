#ifndef _WMIIV_CONTAINER_H
#define _WMIIV_CONTAINER_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "wmiiv/tree/node.h"

struct wmiiv_view;
struct wmiiv_seat;

enum wmiiv_container_layout {
	L_NONE,
	L_HORIZ,
	L_VERT,
	L_STACKED,
	L_TABBED,
};

enum wmiiv_container_border {
	B_NONE,
	B_PIXEL,
	B_NORMAL,
	B_CSD,
};

enum wmiiv_fullscreen_mode {
	FULLSCREEN_NONE,
	FULLSCREEN_WORKSPACE,
	FULLSCREEN_GLOBAL,
};

struct wmiiv_root;
struct wmiiv_output;
struct wmiiv_workspace;
struct wmiiv_view;

enum wlr_direction;

struct wmiiv_container_state {
	// Container properties
	enum wmiiv_container_layout layout;
	double x, y;
	double width, height;

	enum wmiiv_fullscreen_mode fullscreen_mode;

	struct wmiiv_workspace *workspace;
	struct wmiiv_container *parent;    // NULL if container in root of workspace
	list_t *children;                 // struct wmiiv_container

	struct wmiiv_container *focused_inactive_child;
	bool focused;

	enum wmiiv_container_border border;
	int border_thickness;
	bool border_top;
	bool border_bottom;
	bool border_left;
	bool border_right;

	// These are in layout coordinates.
	double content_x, content_y;
	double content_width, content_height;
};

struct wmiiv_container {
	struct wmiiv_node node;
	struct wmiiv_view *view;

	struct wmiiv_container_state current;
	struct wmiiv_container_state pending;

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
	enum wmiiv_container_border saved_border;

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

// TODO (wmiiv) Delete whole module
#include "wmiiv/tree/column.h"
#include "wmiiv/tree/window.h"


bool container_is_column(struct wmiiv_container *container);
bool container_is_window(struct wmiiv_container *container);

void container_destroy(struct wmiiv_container *container);

void container_begin_destroy(struct wmiiv_container *container);
bool container_has_urgent_child(struct wmiiv_container *container);

/**
 * If the container is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void container_end_mouse_operation(struct wmiiv_container *container);

/**
 * Walk up the container tree branch starting at the given container, and return
 * its earliest ancestor.
 */
struct wmiiv_container *container_toplevel_ancestor(
		struct wmiiv_container *container);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 * If the container is not on any output, return NULL.
 */
struct wmiiv_output *container_get_effective_output(struct wmiiv_container *container);

void container_discover_outputs(struct wmiiv_container *container);

enum wmiiv_container_layout container_parent_layout(struct wmiiv_container *container);

enum wmiiv_container_layout container_current_parent_layout(
		struct wmiiv_container *container);

void container_swap(struct wmiiv_container *container1, struct wmiiv_container *container2);

#endif
