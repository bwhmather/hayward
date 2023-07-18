#ifndef HWD_TREE_H
#define HWD_TREE_H

#include <wlr/types/wlr_output_layout.h>

#include <hayward/output.h>
#include <hayward/tree/column.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

void
hwd_move_window_to_floating(struct hwd_window *window);

void
hwd_move_window_to_tiling(struct hwd_window *window);

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
hwd_move_window_to_column_from_direction(
    struct hwd_window *window, struct hwd_column *column, enum wlr_direction move_dir
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
hwd_move_window_to_column(struct hwd_window *window, struct hwd_column *destination);

void
hwd_move_window_to_workspace(struct hwd_window *window, struct hwd_workspace *workspace);

void
hwd_move_window_to_output_from_direction(
    struct hwd_window *window, struct hwd_output *output, enum wlr_direction move_dir
);

void
hwd_move_window_to_output(struct hwd_window *window, struct hwd_output *output);

#endif
