#ifndef _WMIIV_WINDOW_H
#define _WMIIV_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include "list.h"
#include "wmiiv/tree/node.h"

enum wmiiv_window_border {
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

struct wmiiv_seat;
struct wmiiv_root;
struct wmiiv_output;
struct wmiiv_workspace;
struct wmiiv_view;

struct wmiiv_window_state {
	// Container properties
	double x, y;
	double width, height;

	enum wmiiv_fullscreen_mode fullscreen_mode;

	struct wmiiv_workspace *workspace;
	struct wmiiv_column *parent;

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

struct wmiiv_window {
	struct wmiiv_node node;
	struct wmiiv_view *view;

	struct wmiiv_window_state current;
	struct wmiiv_window_state pending;

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

struct wmiiv_window *window_create(struct wmiiv_view *view);

void window_destroy(struct wmiiv_window *window);

void window_begin_destroy(struct wmiiv_window *window);

void window_detach(struct wmiiv_window *window);

/**
 * If the window is involved in a drag or resize operation via a mouse, this
 * ends the operation.
 */
void window_end_mouse_operation(struct wmiiv_window *window);

/**
 * Find any container that has the given mark and return it.
 */
struct wmiiv_window *window_find_mark(char *mark);

/**
 * Find any container that has the given mark and remove the mark from the
 * container. Returns true if it matched a container.
 */
bool window_find_and_unmark(char *mark);

/**
 * Remove all marks from the container.
 */
void window_clear_marks(struct wmiiv_window *container);

bool window_has_mark(struct wmiiv_window *container, char *mark);

void window_add_mark(struct wmiiv_window *container, char *mark);

void window_update_marks_textures(struct wmiiv_window *container);

bool window_is_floating(struct wmiiv_window *container);

/**
 * Same as `window_is_floating`, but for current container state.
 */
bool window_is_current_floating(struct wmiiv_window *container);

void window_set_floating(struct wmiiv_window *container, bool enable);

bool window_is_fullscreen(struct wmiiv_window *container);

bool window_is_tiling(struct wmiiv_window *container);

/**
 * Detaches a window from its current column, and by extension workspace, and
 * attaches it to a new column, possibly in a different workspace.  Where in
 * the new column it is inserted is determined by the direction of movement.
 *
 * If the window was previously floating or fullscreen, all of that state will
 * be cleared.
 *
 * The window will not be automatically focused.
 *
 * The old workspace and old columns will not be automatically cleaned up.
 */
void window_move_to_column_from_direction(
		struct wmiiv_window *window, struct wmiiv_column *column,
		enum wlr_direction move_dir);

/**
 * Detaches a window from its current column, and by extension workspace, and
 * attaches it to a new column, possibly in a different workspace.  The window
 * will be move immediately after the column's last focused child.
 *
 * If the window was previously floating or fullscreen, all of that state will
 * be cleared.
 *
 * The window will not be automatically focused.
 *
 * The old workspace and old columns will not be automatically cleaned up.
 */
void window_move_to_column(struct wmiiv_window *window,
		struct wmiiv_column *destination);

/**
 * Detaches a window from its current workspace and column, and attaches it to
 * a different one.  The new column, and location within that column, are
 * determined by the direction of movement.
 *
 * If the window is floating or fullscreen, it will remain so.
 *
 * The window will not be automatically focused.
 *
 * The old workspace and old columns will not be automatically cleaned up.
 */
void window_move_to_workspace_from_direction(
		struct wmiiv_window *window, struct wmiiv_workspace *workspace,
		enum wlr_direction move_dir);

void window_move_to_workspace(struct wmiiv_window *window,
		struct wmiiv_workspace *workspace);

struct wlr_surface *window_surface_at(struct wmiiv_window *window, double lx, double ly, double *sx, double *sy);

bool window_contains_point(struct wmiiv_window *window, double lx, double ly);

bool window_contents_contain_point(struct wmiiv_window *window, double lx, double ly);

/**
 * Returns the fullscreen window obstructing this window if it exists.
 */
struct wmiiv_window *window_obstructing_fullscreen_window(struct wmiiv_window *window);

void window_update_title_textures(struct wmiiv_window *window);

/**
 * Return the height of a regular title bar.
 */
size_t window_titlebar_height(void);

void window_set_fullscreen(struct wmiiv_window *window,
		enum wmiiv_fullscreen_mode mode);

void window_fullscreen_disable(struct wmiiv_window *window);

void window_handle_fullscreen_reparent(struct wmiiv_window *window);

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height);

void window_floating_resize_and_center(struct wmiiv_window *window);

void window_floating_set_default_size(struct wmiiv_window *window);

/**
 * Move a floating window by the specified amount.
 */
void window_floating_translate(struct wmiiv_window *window,
		double x_amount, double y_amount);

/**
 * Choose an output for the floating window's new position.
 */
struct wmiiv_output *window_floating_find_output(struct wmiiv_window *window);

/**
 * Move a floating window to a new layout-local position.
 */
void window_floating_move_to(struct wmiiv_window *window,
		double lx, double ly);

/**
 * Move a floating window to the center of the workspace.
 */
void window_floating_move_to_center(struct wmiiv_window *window);

/**
 * Get a window's box in layout coordinates.
 */
void window_get_box(struct wmiiv_window *window, struct wlr_box *box);

void window_set_resizing(struct wmiiv_window *window, bool resizing);

void window_set_geometry_from_content(struct wmiiv_window *window);

bool window_is_transient_for(struct wmiiv_window *child,
		struct wmiiv_window *ancestor);

void window_raise_floating(struct wmiiv_window *window);

bool window_is_sticky(struct wmiiv_window *window);

list_t *window_get_siblings(struct wmiiv_window *window);

int window_sibling_index(struct wmiiv_window *child);

list_t *window_get_current_siblings(struct wmiiv_window *window);

struct wmiiv_window *window_get_previous_sibling(struct wmiiv_window *window);
struct wmiiv_window *window_get_next_sibling(struct wmiiv_window *window);

enum wmiiv_column_layout window_parent_layout(struct wmiiv_window *window);

enum wmiiv_column_layout window_current_parent_layout(struct wmiiv_window *window);

void window_swap(struct wmiiv_window *window1, struct wmiiv_window *window2);

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 * If the container is not on any output, return NULL.
 */
struct wmiiv_output *window_get_effective_output(struct wmiiv_window *window);

void window_discover_outputs(struct wmiiv_window *window);

#endif
