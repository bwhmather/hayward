#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/render/drm_format_set.h>
#include "linux-dmabuf-unstable-v1-protocol.h"
#include "cairo_util.h"
#include "pango.h"
#include "sway/config.h"
#include "sway/desktop.h"
#include "sway/desktop/transaction.h"
#include "sway/input/input-manager.h"
#include "sway/input/seat.h"
#include "sway/ipc-server.h"
#include "sway/output.h"
#include "sway/server.h"
#include "sway/tree/arrange.h"
#include "sway/tree/view.h"
#include "sway/tree/workspace.h"
#include "sway/xdg_decoration.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

struct sway_container *window_create(struct sway_view *view) {
	struct sway_container *c = calloc(1, sizeof(struct sway_container));
	if (!c) {
		sway_log(SWAY_ERROR, "Unable to allocate sway_container");
		return NULL;
	}
	node_init(&c->node, N_WINDOW, c);
	c->pending.layout = L_NONE;
	c->view = view;
	c->alpha = 1.0f;

	c->marks = create_list();
	c->outputs = create_list();

	wl_signal_init(&c->events.destroy);
	wl_signal_emit(&root->events.new_node, &c->node);

	return c;
}

static bool find_by_mark_iterator(struct sway_container *con, void *data) {
	char *mark = data;
	if (!container_is_window(con)) {
		return false;
	}

	if (!window_has_mark(con, mark)) {
		return false;
	}

	return true;
}

struct sway_container *window_find_mark(char *mark) {
	return root_find_container(find_by_mark_iterator, mark);
}

bool window_find_and_unmark(char *mark) {
	struct sway_container *con = root_find_container(
		find_by_mark_iterator, mark);
	if (!con) {
		return false;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		char *con_mark = con->marks->items[i];
		if (strcmp(con_mark, mark) == 0) {
			free(con_mark);
			list_del(con->marks, i);
			window_update_marks_textures(con);
			ipc_event_window(con, "mark");
			return true;
		}
	}
	return false;
}

void window_clear_marks(struct sway_container *con) {
	if (!sway_assert(container_is_window(con), "Cannot only clear marks on windows")) {
		return;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		free(con->marks->items[i]);
	}
	con->marks->length = 0;
	ipc_event_window(con, "mark");
}

bool window_has_mark(struct sway_container *con, char *mark) {
	if (!sway_assert(container_is_window(con), "Cannot only check marks on windows")) {
		return false;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		char *item = con->marks->items[i];
		if (strcmp(item, mark) == 0) {
			return true;
		}
	}
	return false;
}

void window_add_mark(struct sway_container *con, char *mark) {
	if (!sway_assert(container_is_window(con), "Cannot only mark windows")) {
		return;
	}

	list_add(con->marks, strdup(mark));
	ipc_event_window(con, "mark");
}

static void render_titlebar_text_texture(struct sway_output *output,
		struct sway_container *con, struct wlr_texture **texture,
		struct border_colors *class, bool pango_markup, char *text) {
	double scale = output->wlr_output->scale;
	int width = 0;
	int height = config->font_height * scale;
	int baseline;

	// We must use a non-nil cairo_t for cairo_set_font_options to work.
	// Therefore, we cannot use cairo_create(NULL).
	cairo_surface_t *dummy_surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, 0, 0);
	cairo_t *c = cairo_create(dummy_surface);
	cairo_set_antialias(c, CAIRO_ANTIALIAS_BEST);
	cairo_font_options_t *fo = cairo_font_options_create();
	cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
	if (output->wlr_output->subpixel == WL_OUTPUT_SUBPIXEL_NONE) {
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
	} else {
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
		cairo_font_options_set_subpixel_order(fo,
			to_cairo_subpixel_order(output->wlr_output->subpixel));
	}
	cairo_set_font_options(c, fo);
	get_text_size(c, config->font, &width, NULL, &baseline, scale,
			config->pango_markup, "%s", text);
	cairo_surface_destroy(dummy_surface);
	cairo_destroy(c);

	if (width == 0 || height == 0) {
		return;
	}

	if (height > config->font_height * scale) {
		height = config->font_height * scale;
	}

	cairo_surface_t *surface = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, width, height);
	cairo_status_t status = cairo_surface_status(surface);
	if (status != CAIRO_STATUS_SUCCESS) {
		sway_log(SWAY_ERROR, "cairo_image_surface_create failed: %s",
			cairo_status_to_string(status));
		return;
	}

	cairo_t *cairo = cairo_create(surface);
	cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
	cairo_set_font_options(cairo, fo);
	cairo_font_options_destroy(fo);
	cairo_set_source_rgba(cairo, class->background[0], class->background[1],
			class->background[2], class->background[3]);
	cairo_paint(cairo);
	PangoContext *pango = pango_cairo_create_context(cairo);
	cairo_set_source_rgba(cairo, class->text[0], class->text[1],
			class->text[2], class->text[3]);
	cairo_move_to(cairo, 0, config->font_baseline * scale - baseline);

	render_text(cairo, config->font, scale, pango_markup, "%s", text);

	cairo_surface_flush(surface);
	unsigned char *data = cairo_image_surface_get_data(surface);
	int stride = cairo_image_surface_get_stride(surface);
	struct wlr_renderer *renderer = output->wlr_output->renderer;
	*texture = wlr_texture_from_pixels(
			renderer, DRM_FORMAT_ARGB8888, stride, width, height, data);
	cairo_surface_destroy(surface);
	g_object_unref(pango);
	cairo_destroy(cairo);
}

static void update_marks_texture(struct sway_container *con,
		struct wlr_texture **texture, struct border_colors *class) {
	struct sway_output *output = container_get_effective_output(con);
	if (!output) {
		return;
	}
	if (*texture) {
		wlr_texture_destroy(*texture);
		*texture = NULL;
	}
	if (!con->marks->length) {
		return;
	}

	size_t len = 0;
	for (int i = 0; i < con->marks->length; ++i) {
		char *mark = con->marks->items[i];
		if (mark[0] != '_') {
			len += strlen(mark) + 2;
		}
	}
	char *buffer = calloc(len + 1, 1);
	char *part = malloc(len + 1);

	if (!sway_assert(buffer && part, "Unable to allocate memory")) {
		free(buffer);
		return;
	}

	for (int i = 0; i < con->marks->length; ++i) {
		char *mark = con->marks->items[i];
		if (mark[0] != '_') {
			snprintf(part, len + 1, "[%s]", mark);
			strcat(buffer, part);
		}
	}
	free(part);

	render_titlebar_text_texture(output, con, texture, class, false, buffer);

	free(buffer);
}

void window_update_marks_textures(struct sway_container *con) {
	if (!sway_assert(container_is_window(con), "Only windows have marks textures")) {
		return;
	}

	if (!config->show_marks) {
		return;
	}
	update_marks_texture(con, &con->marks_focused,
			&config->border_colors.focused);
	update_marks_texture(con, &con->marks_focused_inactive,
			&config->border_colors.focused_inactive);
	update_marks_texture(con, &con->marks_unfocused,
			&config->border_colors.unfocused);
	update_marks_texture(con, &con->marks_urgent,
			&config->border_colors.urgent);
	update_marks_texture(con, &con->marks_focused_tab_title,
			&config->border_colors.focused_tab_title);
	container_damage_whole(con);
}

void window_set_floating(struct sway_container *win, bool enable) {
	if (!sway_assert(container_is_window(win), "Can only float windows")) {
		return;
	}

	if (container_is_floating(win) == enable) {
		return;
	}

	struct sway_seat *seat = input_manager_current_seat();
	struct sway_workspace *workspace = win->pending.workspace;
	struct sway_container *focus = seat_get_focused_container(seat);
	bool set_focus = focus == win;

	if (enable) {
		struct sway_container *old_parent = win->pending.parent;
		container_detach(win);
		workspace_add_floating(workspace, win);
		view_set_tiled(win->view, false);
		if (win->view->using_csd) {
			win->saved_border = win->pending.border;
			win->pending.border = B_CSD;
			if (win->view->xdg_decoration) {
				struct sway_xdg_decoration *deco = win->view->xdg_decoration;
				wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
						WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
			}
		}
		container_floating_set_default_size(win);
		container_floating_resize_and_center(win);
		if (old_parent) {
			if (set_focus) {
				seat_set_raw_focus(seat, &old_parent->node);
				seat_set_raw_focus(seat, &win->node);
			}
			column_consider_destroy(old_parent);
		}
	} else {
		// Returning to tiled
		container_detach(win);
		struct sway_container *reference =
			seat_get_focus_inactive_tiling(seat, workspace);
		if (reference) {
			if (reference->view) {
				column_add_sibling(reference, win, 1);
			} else {
				column_add_child(reference, win);
			}
			win->pending.width = reference->pending.width;
			win->pending.height = reference->pending.height;
		} else {
			struct sway_container *other =
				workspace_add_tiling(workspace, win);
			other->pending.width = workspace->width;
			other->pending.height = workspace->height;
		}
		if (win->view) {
			view_set_tiled(win->view, true);
			if (win->view->using_csd) {
				win->pending.border = win->saved_border;
				if (win->view->xdg_decoration) {
					struct sway_xdg_decoration *deco = win->view->xdg_decoration;
					wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
							WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
				}
			}
		}
		win->width_fraction = 0;
		win->height_fraction = 0;
	}

	container_end_mouse_operation(win);

	ipc_event_window(win, "floating");
}

/**
 * Ensures all seats focus the fullscreen container if needed.
 */
static void workspace_focus_fullscreen(struct sway_workspace *workspace) {
	// TODO (wmiiv) move me.
	if (!workspace->fullscreen) {
		return;
	}
	struct sway_seat *seat;
	struct sway_workspace *focus_ws;
	wl_list_for_each(seat, &server.input->seats, link) {
		focus_ws = seat_get_focused_workspace(seat);
		if (focus_ws == workspace) {
			struct sway_node *new_focus =
				seat_get_focus_inactive(seat, &workspace->fullscreen->node);
			seat_set_raw_focus(seat, new_focus);
		}
	}
}

static void window_move_to_column_from_maybe_direction(
		struct sway_container *win, struct sway_container *col,
		bool has_move_dir, enum wlr_direction move_dir) {
	if (!sway_assert(container_is_window(win), "Can only move windows to columns")) {
		return;
	}
	if (!sway_assert(container_is_column(col), "Can only move windows to columns")) {
		return;
	}

	if (win->pending.parent == col) {
		return;
	}

	struct sway_seat *seat = input_manager_get_default_seat();
	struct sway_workspace *old_workspace = win->pending.workspace;

	if (has_move_dir && (move_dir == WLR_DIRECTION_UP || move_dir == WLR_DIRECTION_DOWN)) {
		sway_log(SWAY_DEBUG, "Reparenting window (parallel)");
		int index =
			move_dir == WLR_DIRECTION_DOWN ?
			0 : col->pending.children->length;
		column_insert_child(col, win, index);
		win->pending.width = win->pending.height = 0;
		win->width_fraction = win->height_fraction = 0;
	} else {
		sway_log(SWAY_DEBUG, "Reparenting window (perpendicular)");
		struct sway_container *target_sibling = seat_get_focus_inactive_view(seat, &col->node);
		if (target_sibling) {
			column_add_sibling(target_sibling, win, 1);
		} else {
			column_add_child(col, win);
		}
	}

	ipc_event_window(win, "move");

	if (col->pending.workspace) {
		workspace_focus_fullscreen(col->pending.workspace);
		workspace_detect_urgent(col->pending.workspace);
	}

	if (old_workspace && old_workspace != col->pending.workspace) {
		workspace_detect_urgent(old_workspace);
	}
}

void window_move_to_column_from_direction(
		struct sway_container *win, struct sway_container *col,
		enum wlr_direction move_dir) {
	window_move_to_column_from_maybe_direction(win, col, true, move_dir);
}

void window_move_to_column(struct sway_container *win,
		struct sway_container *col) {
	window_move_to_column_from_maybe_direction(win, col, false, WLR_DIRECTION_DOWN);
}

static void window_move_to_workspace_from_maybe_direction(
		struct sway_container *win, struct sway_workspace *ws,
		bool has_move_dir, enum wlr_direction move_dir) {
	if (!sway_assert(container_is_window(win), "Can only move windows between workspaces")) {
		return;
	}

	if (ws == win->pending.workspace) {
		return;
	}

	struct sway_seat *seat = input_manager_get_default_seat();

	// TODO (wmiiv) fullscreen.

	if (container_is_floating(win)) {
		struct sway_output *old_output = win->pending.workspace->output;
		container_detach(win);
		workspace_add_floating(ws, win);
		container_handle_fullscreen_reparent(win);
		// If changing output, center it within the workspace
		if (old_output != ws->output && !win->pending.fullscreen_mode) {
			container_floating_move_to_center(win);
		}

		return;
	}

	win->pending.width = win->pending.height = 0;
	win->width_fraction = win->height_fraction = 0;

	if (!ws->tiling->length) {
		struct sway_container *col = column_create();
		workspace_insert_tiling_direct(ws, col, 0);
	}

	if (has_move_dir && (move_dir == WLR_DIRECTION_LEFT || move_dir == WLR_DIRECTION_RIGHT)) {
		sway_log(SWAY_DEBUG, "Reparenting window (parallel)");
		// Move to either left-most or right-most column based on move
		// direction.
		int index =
			move_dir == WLR_DIRECTION_RIGHT ?
			0 : ws->tiling->length;
		struct sway_container *col = ws->tiling->items[index];
		window_move_to_column_from_maybe_direction(win, col, has_move_dir, move_dir);
	} else {
		sway_log(SWAY_DEBUG, "Reparenting container (perpendicular)");
		// Move to the most recently focused column in the workspace.
		struct sway_container *col = NULL;

		struct sway_container *focus_inactive = seat_get_focus_inactive_tiling(seat, ws);
		// TODO (wmiiv) only windows should be focusable.
		focus_inactive = seat_get_focus_inactive_view(seat, &focus_inactive->node);
		if (focus_inactive) {
			col = focus_inactive->pending.parent;
		} else {
			col = ws->tiling->items[0];
		}
		window_move_to_column_from_maybe_direction(win, col, has_move_dir, move_dir);
	}
}

void window_move_to_workspace_from_direction(
		struct sway_container *win, struct sway_workspace *ws,
		enum wlr_direction move_dir) {
	window_move_to_workspace_from_maybe_direction(win, ws, true, move_dir);
}

void window_move_to_workspace(struct sway_container *win,
		struct sway_workspace *ws) {
	window_move_to_workspace_from_maybe_direction(win, ws, false, WLR_DIRECTION_DOWN);
}
