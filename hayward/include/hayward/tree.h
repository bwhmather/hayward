#ifndef _HAYWARD_TREE_H
#define _HAYWARD_TREE_H

#include <wlr/types/wlr_output_layout.h>

#include <hayward/output.h>
#include <hayward/tree/column.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

void
hayward_move_window_to_floating(struct hayward_window *window);

void
hayward_move_window_to_tiling(struct hayward_window *window);

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
void
hayward_move_window_to_column_from_direction(
    struct hayward_window *window, struct hayward_column *column,
    enum wlr_direction move_dir
);

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
void
hayward_move_window_to_column(
    struct hayward_window *window, struct hayward_column *destination
);

void
hayward_move_window_to_workspace(
    struct hayward_window *window, struct hayward_workspace *workspace
);

void
hayward_move_window_to_output_from_direction(
    struct hayward_window *window, struct hayward_output *output,
    enum wlr_direction move_dir
);

void
hayward_move_window_to_output(
    struct hayward_window *window, struct hayward_output *output
);

#endif
