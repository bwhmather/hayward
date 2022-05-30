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
		struct wmiiv_container *window, struct wmiiv_workspace *ws,
		enum wlr_direction move_dir);

void window_move_to_workspace(struct wmiiv_container *window,
		struct wmiiv_workspace *ws);

struct wlr_surface *window_surface_at(struct wmiiv_container *window, double lx, double ly, double *sx, double *sy);

bool window_contains_point(struct wmiiv_container *window, double lx, double ly);

bool window_contents_contain_point(struct wmiiv_container *window, double lx, double ly);

#endif
