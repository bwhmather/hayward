#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/input/cursor.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"

// A couple of things here:
// - view->border should never be B_CSD when the view is tiled, even when CSD is
//   in use (we set using_csd instead and render a wmiiv border).
// - view->saved_border should be the last applied border when switching to CSD.
// - view->using_csd should always reflect whether CSD is applied or not.
static void set_border(struct wmiiv_container *win,
		enum wmiiv_container_border new_border) {
	if (win->view->using_csd && new_border != B_CSD) {
		view_set_csd_from_server(win->view, false);
	} else if (!win->view->using_csd && new_border == B_CSD) {
		view_set_csd_from_server(win->view, true);
		win->saved_border = win->pending.border;
	}

	if (new_border != B_CSD || window_is_floating(win)) {
		win->pending.border = new_border;
	}
	win->view->using_csd = new_border == B_CSD;
}

static void border_toggle(struct wmiiv_container *win) {
	if (win->view->using_csd) {
		set_border(win, B_NONE);
		return;
	}
	switch (win->pending.border) {
	case B_NONE:
		set_border(win, B_PIXEL);
		break;
	case B_PIXEL:
		set_border(win, B_NORMAL);
		break;
	case B_NORMAL:
		if (win->view->xdg_decoration) {
			set_border(win, B_CSD);
		} else {
			set_border(win, B_NONE);
		}
		break;
	case B_CSD:
		// view->using_csd should be true so it would have returned above
		wmiiv_assert(false, "Unreachable");
		break;
	}
}

struct cmd_results *cmd_border(int argc, char **argv) {
	struct cmd_results *error = NULL;
	if ((error = checkarg(argc, "border", EXPECTED_AT_LEAST, 1))) {
		return error;
	}

	struct wmiiv_container *win = config->handler_context.window;
	if (!win) {
		return cmd_results_new(CMD_INVALID, "Only windows can have borders");
	}
	struct wmiiv_view *view = win->view;

	if (strcmp(argv[0], "none") == 0) {
		set_border(win, B_NONE);
	} else if (strcmp(argv[0], "normal") == 0) {
		set_border(win, B_NORMAL);
	} else if (strcmp(argv[0], "pixel") == 0) {
		set_border(win, B_PIXEL);
	} else if (strcmp(argv[0], "csd") == 0) {
		if (!view->xdg_decoration) {
			return cmd_results_new(CMD_INVALID,
					"This window doesn't support client side decorations");
		}
		set_border(win, B_CSD);
	} else if (strcmp(argv[0], "toggle") == 0) {
		border_toggle(win);
	} else {
		return cmd_results_new(CMD_INVALID,
				"Expected 'border <none|normal|pixel|csd|toggle>' "
				"or 'border pixel <px>'");
	}
	if (argc == 2) {
		win->pending.border_thickness = atoi(argv[1]);
	}

	if (window_is_floating(win)) {
		container_set_geometry_from_content(win);
	}

	arrange_window(win);

	return cmd_results_new(CMD_SUCCESS, NULL);
}
