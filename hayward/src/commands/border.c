#include "hayward-common/log.h"

#include "hayward/commands.h"
#include "hayward/config.h"
#include "hayward/input/cursor.h"
#include "hayward/input/input-manager.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/view.h"
#include "hayward/tree/window.h"

// A couple of things here:
// - view->border should never be B_CSD when the view is tiled, even when CSD is
//   in use (we set using_csd instead and render a hayward border).
// - view->saved_border should be the last applied border when switching to CSD.
// - view->using_csd should always reflect whether CSD is applied or not.
static void set_border(
    struct hayward_window *window, enum hayward_window_border new_border
) {
    if (window->view->using_csd && new_border != B_CSD) {
        view_set_csd_from_server(window->view, false);
    } else if (!window->view->using_csd && new_border == B_CSD) {
        view_set_csd_from_server(window->view, true);
        window->saved_border = window->pending.border;
    }

    if (new_border != B_CSD || window_is_floating(window)) {
        window->pending.border = new_border;
    }
    window->view->using_csd = new_border == B_CSD;
}

static void border_toggle(struct hayward_window *window) {
    if (window->view->using_csd) {
        set_border(window, B_NONE);
        return;
    }
    switch (window->pending.border) {
    case B_NONE:
        set_border(window, B_PIXEL);
        break;
    case B_PIXEL:
        set_border(window, B_NORMAL);
        break;
    case B_NORMAL:
        if (window->view->xdg_decoration) {
            set_border(window, B_CSD);
        } else {
            set_border(window, B_NONE);
        }
        break;
    case B_CSD:
        // view->using_csd should be true so it would have returned above
        hayward_assert(false, "Unreachable");
        break;
    }
}

struct cmd_results *cmd_border(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "border", EXPECTED_AT_LEAST, 1))) {
        return error;
    }

    struct hayward_window *window = config->handler_context.window;
    if (!window) {
        return cmd_results_new(CMD_INVALID, "Only windows can have borders");
    }
    struct hayward_view *view = window->view;

    if (strcmp(argv[0], "none") == 0) {
        set_border(window, B_NONE);
    } else if (strcmp(argv[0], "normal") == 0) {
        set_border(window, B_NORMAL);
    } else if (strcmp(argv[0], "pixel") == 0) {
        set_border(window, B_PIXEL);
    } else if (strcmp(argv[0], "csd") == 0) {
        if (!view->xdg_decoration) {
            return cmd_results_new(
                CMD_INVALID,
                "This window doesn't support client side decorations"
            );
        }
        set_border(window, B_CSD);
    } else if (strcmp(argv[0], "toggle") == 0) {
        border_toggle(window);
    } else {
        return cmd_results_new(
            CMD_INVALID,
            "Expected 'border <none|normal|pixel|csd|toggle>' "
            "or 'border pixel <px>'"
        );
    }
    if (argc == 2) {
        window->pending.border_thickness = atoi(argv[1]);
    }

    if (window_is_floating(window)) {
        window_set_geometry_from_content(window);
    }

    arrange_window(window);

    return cmd_results_new(CMD_SUCCESS, NULL);
}
