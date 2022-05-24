#ifndef _SWAY_WINDOW_H
#define _SWAY_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_container *window_create(struct sway_view *view);

/**
 * Find any container that has the given mark and return it.
 */
struct sway_container *window_find_mark(char *mark);

/**
 * Find any container that has the given mark and remove the mark from the
 * container. Returns true if it matched a container.
 */
bool window_find_and_unmark(char *mark);

/**
 * Remove all marks from the container.
 */
void window_clear_marks(struct sway_container *container);

bool window_has_mark(struct sway_container *container, char *mark);

void window_add_mark(struct sway_container *container, char *mark);

void window_update_marks_textures(struct sway_container *container);

bool window_is_floating(struct sway_container *container);

/**
 * Same as `window_is_floating`, but for current container state.
 */
bool window_is_current_floating(struct sway_container *container);

void window_set_floating(struct sway_container *container, bool enable);

bool window_is_fullscreen(struct sway_container *container);

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
		struct sway_container *win, struct sway_container *col,
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
void window_move_to_column(struct sway_container *win,
		struct sway_container *destination);

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
		struct sway_container *win, struct sway_workspace *ws,
		enum wlr_direction move_dir);

void window_move_to_workspace(struct sway_container *win,
		struct sway_workspace *ws);

#endif
