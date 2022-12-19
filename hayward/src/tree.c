#define _POSIX_C_SOURCE 200809L
#include "hayward/tree.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wayland-server-core.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>

#include "hayward-common/cairo_util.h"
#include "hayward-common/list.h"
#include "hayward-common/log.h"
#include "hayward-common/pango.h"
#include "hayward-common/stringop.h"

#include "linux-dmabuf-unstable-v1-protocol.h"

#include "hayward/config.h"
#include "hayward/desktop.h"
#include "hayward/desktop/transaction.h"
#include "hayward/input/input-manager.h"
#include "hayward/input/seat.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/server.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "hayward/xdg_decoration.h"

static void
window_set_floating(struct hayward_window *window, bool enable) {
    hayward_assert(window != NULL, "Expected window");

    if (window_is_floating(window) == enable) {
        return;
    }

    struct hayward_workspace *workspace = window->pending.workspace;
    hayward_assert(workspace != NULL, "Window not attached to workspace");

    bool focus = workspace_get_active_window(workspace);

    if (enable) {
        struct hayward_column *old_parent = window->pending.parent;
        window_detach(window);
        workspace_add_floating(workspace, window);
        view_set_tiled(window->view, false);
        if (window->view->using_csd) {
            window->saved_border = window->pending.border;
            window->pending.border = B_CSD;
            if (window->view->xdg_decoration) {
                struct hayward_xdg_decoration *deco =
                    window->view->xdg_decoration;
                wlr_xdg_toplevel_decoration_v1_set_mode(
                    deco->wlr_xdg_decoration,
                    WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                );
            }
        }
        window_floating_set_default_size(window);
        window_floating_resize_and_center(window);
        if (old_parent) {
            column_consider_destroy(old_parent);
        }
    } else {
        // Returning to tiled
        struct hayward_output *output = window_get_output(window);

        window_detach(window);
        if (window->view) {
            view_set_tiled(window->view, true);
            if (window->view->using_csd) {
                window->pending.border = window->saved_border;
                if (window->view->xdg_decoration) {
                    struct hayward_xdg_decoration *deco =
                        window->view->xdg_decoration;
                    wlr_xdg_toplevel_decoration_v1_set_mode(
                        deco->wlr_xdg_decoration,
                        WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                    );
                }
            }
        }
        window->width_fraction = 0;
        window->height_fraction = 0;

        struct hayward_column *column = NULL;
        for (int i = 0; i < workspace->pending.tiling->length; i++) {
            struct hayward_column *candidate_column =
                workspace->pending.tiling->items[i];

            if (candidate_column->pending.output != output) {
                continue;
            }

            if (column != NULL) {
                continue;
            }

            column = candidate_column;
        }
        if (workspace->pending.active_column != NULL &&
            workspace->pending.active_column->pending.output == output) {
            column = workspace->pending.active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_tiling(workspace, output, column, 0);
        }

        hayward_move_window_to_column(window, column);
    }

    if (focus) {
        workspace_set_active_window(workspace, window);
    }

    window_end_mouse_operation(window);

    ipc_event_window(window, "floating");
}

void
hayward_move_window_to_floating(struct hayward_window *window) {
    window_set_floating(window, true);
}

void
hayward_move_window_to_tiling(struct hayward_window *window) {
    window_set_floating(window, false);
}

static void
hayward_move_window_to_column_from_maybe_direction(
    struct hayward_window *window, struct hayward_column *column,
    bool has_move_dir, enum wlr_direction move_dir
) {
    if (window->pending.parent == column) {
        return;
    }

    struct hayward_workspace *old_workspace = window->pending.workspace;

    if (has_move_dir &&
        (move_dir == WLR_DIRECTION_UP || move_dir == WLR_DIRECTION_DOWN)) {
        hayward_log(HAYWARD_DEBUG, "Reparenting window (parallel)");
        int index = move_dir == WLR_DIRECTION_DOWN
            ? 0
            : column->pending.children->length;
        window_detach(window);
        column_insert_child(column, window, index);
        window->pending.width = window->pending.height = 0;
        window->width_fraction = window->height_fraction = 0;
    } else {
        hayward_log(HAYWARD_DEBUG, "Reparenting window (perpendicular)");
        struct hayward_window *target_sibling = column->pending.active_child;
        window_detach(window);
        if (target_sibling) {
            column_add_sibling(target_sibling, window, 1);
        } else {
            column_add_child(column, window);
        }
    }

    ipc_event_window(window, "move");

    if (column->pending.workspace) {
        workspace_detect_urgent(column->pending.workspace);
    }

    if (old_workspace && old_workspace != column->pending.workspace) {
        workspace_detect_urgent(old_workspace);
    }
}

void
hayward_move_window_to_column_from_direction(
    struct hayward_window *window, struct hayward_column *column,
    enum wlr_direction move_dir
) {
    hayward_move_window_to_column_from_maybe_direction(
        window, column, true, move_dir
    );
}

void
hayward_move_window_to_column(
    struct hayward_window *window, struct hayward_column *column
) {
    hayward_move_window_to_column_from_maybe_direction(
        window, column, false, WLR_DIRECTION_DOWN
    );
}

void
hayward_move_window_to_workspace(
    struct hayward_window *window, struct hayward_workspace *workspace
) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(workspace != NULL, "Expected workspace");

    if (workspace == window->pending.workspace) {
        return;
    }

    if (window_is_floating(window)) {
        window_detach(window);
        workspace_add_floating(workspace, window);
        window_handle_fullscreen_reparent(window);
    } else {
        struct hayward_output *output = window->pending.parent->pending.output;
        struct hayward_column *column = NULL;

        for (int i = 0; i < workspace->pending.tiling->length; i++) {
            struct hayward_column *candidate_column =
                workspace->pending.tiling->items[i];

            if (candidate_column->pending.output != output) {
                continue;
            }

            if (column != NULL) {
                continue;
            }

            column = candidate_column;
        }
        if (workspace->pending.active_column != NULL &&
            workspace->pending.active_column->pending.output == output) {
            column = workspace->pending.active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_tiling(workspace, output, column, 0);
        }

        window->pending.width = window->pending.height = 0;
        window->width_fraction = window->height_fraction = 0;

        hayward_move_window_to_column(window, column);
    }
}

void
hayward_move_window_to_output_from_direction(
    struct hayward_window *window, struct hayward_output *output,
    enum wlr_direction move_dir
) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(output != NULL, "Expected output");

    struct hayward_workspace *workspace = window->pending.workspace;
    hayward_assert(workspace != NULL, "Window is not attached to a workspace");

    // TODO this should be derived from the window's current position.
    struct hayward_output *old_output = workspace_get_active_output(workspace);
    if (window_is_floating(window)) {
        if (old_output != output && !window->pending.fullscreen) {
            window_floating_move_to_center(window);
        }

        return;
    } else {
        struct hayward_column *column = NULL;

        for (int i = 0; i < workspace->pending.tiling->length; i++) {
            struct hayward_column *candidate_column =
                workspace->pending.tiling->items[i];

            if (candidate_column->pending.output != output) {
                continue;
            }

            if (move_dir == WLR_DIRECTION_LEFT || column == NULL) {
                column = candidate_column;
            }
        }
        if (workspace->pending.active_column->pending.output == output &&
            move_dir != WLR_DIRECTION_UP && move_dir == WLR_DIRECTION_DOWN) {
            column = workspace->pending.active_column;
        }
        if (column == NULL) {
            column = column_create();
            workspace_insert_tiling(workspace, output, column, 0);
        }

        window->pending.width = window->pending.height = 0;
        window->width_fraction = window->height_fraction = 0;

        hayward_move_window_to_column_from_direction(window, column, move_dir);
    }
}

void
hayward_move_window_to_output(
    struct hayward_window *window, struct hayward_output *output
) {
    hayward_move_window_to_output_from_direction(window, output, 0);
}
