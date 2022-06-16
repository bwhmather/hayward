#ifndef _WMIIV_WINDOW_H
#define _WMIIV_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "wmiiv/tree/node.h"

struct wmiiv_container *window_create(struct wmiiv_view *view);

void window_detach(struct wmiiv_container *window);

/**
 * Find any container that has the given mark and return it.
 */
struct wmiiv_container *window_find_mark(char *mark);

/**
 * Find any container that has the given mark and remove the mark from the
 * container. Returns true if it matched a container.
 */
bool window_find_and_unmark(char *mark);

/**
 * Remove all marks from the container.
 */
void window_clear_marks(struct wmiiv_container *container);

bool window_has_mark(struct wmiiv_container *container, char *mark);

void window_add_mark(struct wmiiv_container *container, char *mark);

void window_update_marks_textures(struct wmiiv_container *container);

bool window_is_floating(struct wmiiv_container *container);

/**
 * Same as `window_is_floating`, but for current container state.
 */
bool window_is_current_floating(struct wmiiv_container *container);

void window_set_floating(struct wmiiv_container *container, bool enable);

bool window_is_fullscreen(struct wmiiv_container *container);

bool window_is_tiling(struct wmiiv_container *container);

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
		struct wmiiv_container *window, struct wmiiv_container *column,
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
void window_move_to_column(struct wmiiv_container *window,
		struct wmiiv_container *destination);

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
		struct wmiiv_container *window, struct wmiiv_workspace *workspace,
		enum wlr_direction move_dir);

void window_move_to_workspace(struct wmiiv_container *window,
		struct wmiiv_workspace *workspace);

struct wlr_surface *window_surface_at(struct wmiiv_container *window, double lx, double ly, double *sx, double *sy);

bool window_contains_point(struct wmiiv_container *window, double lx, double ly);

bool window_contents_contain_point(struct wmiiv_container *window, double lx, double ly);

/**
 * Returns the fullscreen window obstructing this window if it exists.
 */
struct wmiiv_container *window_obstructing_fullscreen_window(struct wmiiv_container *window);

void window_damage_whole(struct wmiiv_container *window);

void window_update_title_textures(struct wmiiv_container *window);

/**
 * Return the height of a regular title bar.
 */
size_t window_titlebar_height(void);

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height);

void window_floating_resize_and_center(struct wmiiv_container *window);

void window_floating_set_default_size(struct wmiiv_container *window);

/**
 * Move a floating window by the specified amount.
 */
void window_floating_translate(struct wmiiv_container *window,
		double x_amount, double y_amount);

/**
 * Choose an output for the floating window's new position.
 */
struct wmiiv_output *window_floating_find_output(struct wmiiv_container *window);

/**
 * Move a floating window to a new layout-local position.
 */
void window_floating_move_to(struct wmiiv_container *window,
		double lx, double ly);

/**
 * Move a floating window to the center of the workspace.
 */
void window_floating_move_to_center(struct wmiiv_container *window);

/**
 * Get a window's box in layout coordinates.
 */
void window_get_box(struct wmiiv_container *window, struct wlr_box *box);

void window_set_resizing(struct wmiiv_container *window, bool resizing);

void window_set_geometry_from_content(struct wmiiv_container *window);

#endif
