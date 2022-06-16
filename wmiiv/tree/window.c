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
#include "wmiiv/config.h"
#include "wmiiv/desktop.h"
#include "wmiiv/desktop/transaction.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/server.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/xdg_decoration.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

struct wmiiv_container *window_create(struct wmiiv_view *view) {
	struct wmiiv_container *c = calloc(1, sizeof(struct wmiiv_container));
	if (!c) {
		wmiiv_log(WMIIV_ERROR, "Unable to allocate wmiiv_container");
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

void window_detach(struct wmiiv_container *window) {
	if (window->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		window->pending.workspace->fullscreen = NULL;
	}
	if (window->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		root->fullscreen_global = NULL;
	}

	struct wmiiv_container *old_parent = window->pending.parent;
	struct wmiiv_workspace *old_workspace = window->pending.workspace;

	list_t *siblings = container_get_siblings(window);
	if (siblings) {
		int index = list_find(siblings, window);
		if (index != -1) {
			list_del(siblings, index);
		}
	}

	window->pending.parent = NULL;
	window->pending.workspace = NULL;

	if (old_parent) {
		column_update_representation(old_parent);
		node_set_dirty(&old_parent->node);
	} else if (old_workspace) {
		workspace_update_representation(old_workspace);
		node_set_dirty(&old_workspace->node);
	}
	node_set_dirty(&window->node);
}


static bool find_by_mark_iterator(struct wmiiv_container *container, void *data) {
	char *mark = data;
	if (!container_is_window(container)) {
		return false;
	}

	if (!window_has_mark(container, mark)) {
		return false;
	}

	return true;
}

struct wmiiv_container *window_find_mark(char *mark) {
	return root_find_container(find_by_mark_iterator, mark);
}

bool window_find_and_unmark(char *mark) {
	struct wmiiv_container *container = root_find_container(
		find_by_mark_iterator, mark);
	if (!container) {
		return false;
	}

	for (int i = 0; i < container->marks->length; ++i) {
		char *container_mark = container->marks->items[i];
		if (strcmp(container_mark, mark) == 0) {
			free(container_mark);
			list_del(container->marks, i);
			window_update_marks_textures(container);
			ipc_event_window(container, "mark");
			return true;
		}
	}
	return false;
}

void window_clear_marks(struct wmiiv_container *container) {
	if (!wmiiv_assert(container_is_window(container), "Cannot only clear marks on windows")) {
		return;
	}

	for (int i = 0; i < container->marks->length; ++i) {
		free(container->marks->items[i]);
	}
	container->marks->length = 0;
	ipc_event_window(container, "mark");
}

bool window_has_mark(struct wmiiv_container *container, char *mark) {
	if (!wmiiv_assert(container_is_window(container), "Cannot only check marks on windows")) {
		return false;
	}

	for (int i = 0; i < container->marks->length; ++i) {
		char *item = container->marks->items[i];
		if (strcmp(item, mark) == 0) {
			return true;
		}
	}
	return false;
}

void window_add_mark(struct wmiiv_container *container, char *mark) {
	if (!wmiiv_assert(container_is_window(container), "Cannot only mark windows")) {
		return;
	}

	list_add(container->marks, strdup(mark));
	ipc_event_window(container, "mark");
}

static void render_titlebar_text_texture(struct wmiiv_output *output,
		struct wmiiv_container *container, struct wlr_texture **texture,
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
		wmiiv_log(WMIIV_ERROR, "cairo_image_surface_create failed: %s",
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

static void update_marks_texture(struct wmiiv_container *container,
		struct wlr_texture **texture, struct border_colors *class) {
	struct wmiiv_output *output = container_get_effective_output(container);
	if (!output) {
		return;
	}
	if (*texture) {
		wlr_texture_destroy(*texture);
		*texture = NULL;
	}
	if (!container->marks->length) {
		return;
	}

	size_t len = 0;
	for (int i = 0; i < container->marks->length; ++i) {
		char *mark = container->marks->items[i];
		if (mark[0] != '_') {
			len += strlen(mark) + 2;
		}
	}
	char *buffer = calloc(len + 1, 1);
	char *part = malloc(len + 1);

	if (!wmiiv_assert(buffer && part, "Unable to allocate memory")) {
		free(buffer);
		return;
	}

	for (int i = 0; i < container->marks->length; ++i) {
		char *mark = container->marks->items[i];
		if (mark[0] != '_') {
			snprintf(part, len + 1, "[%s]", mark);
			strcat(buffer, part);
		}
	}
	free(part);

	render_titlebar_text_texture(output, container, texture, class, false, buffer);

	free(buffer);
}

void window_update_marks_textures(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Only windows have marks textures")) {
		return;
	}

	if (!config->show_marks) {
		return;
	}
	update_marks_texture(window, &window->marks_focused,
			&config->border_colors.focused);
	update_marks_texture(window, &window->marks_focused_inactive,
			&config->border_colors.focused_inactive);
	update_marks_texture(window, &window->marks_unfocused,
			&config->border_colors.unfocused);
	update_marks_texture(window, &window->marks_urgent,
			&config->border_colors.urgent);
	update_marks_texture(window, &window->marks_focused_tab_title,
			&config->border_colors.focused_tab_title);
	window_damage_whole(window);
}

static void update_title_texture(struct wmiiv_container *window,
		struct wlr_texture **texture, struct border_colors *class) {
	struct wmiiv_output *output = container_get_effective_output(window);
	if (!output) {
		return;
	}
	if (*texture) {
		wlr_texture_destroy(*texture);
		*texture = NULL;
	}
	if (!window->formatted_title) {
		return;
	}

	render_titlebar_text_texture(output, window, texture, class,
		config->pango_markup, window->formatted_title);
}

void window_update_title_textures(struct wmiiv_container *window) {
	update_title_texture(window, &window->title_focused,
			&config->border_colors.focused);
	update_title_texture(window, &window->title_focused_inactive,
			&config->border_colors.focused_inactive);
	update_title_texture(window, &window->title_unfocused,
			&config->border_colors.unfocused);
	update_title_texture(window, &window->title_urgent,
			&config->border_colors.urgent);
	update_title_texture(window, &window->title_focused_tab_title,
			&config->border_colors.focused_tab_title);
	window_damage_whole(window);
}

bool window_is_floating(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Only windows can float")) {
		return false;
	}

	if (!window->pending.parent && window->pending.workspace &&
			list_find(window->pending.workspace->floating, window) != -1) {
		return true;
	}
	return false;
}

bool window_is_current_floating(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Only windows can float")) {
		return false;
	}
	if (!window->current.parent && window->current.workspace &&
			list_find(window->current.workspace->floating, window) != -1) {
		return true;
	}
	return false;
}

void window_set_floating(struct wmiiv_container *window, bool enable) {
	if (!wmiiv_assert(container_is_window(window), "Can only float windows")) {
		return;
	}

	if (window_is_floating(window) == enable) {
		return;
	}

	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_workspace *workspace = window->pending.workspace;

	if (enable) {
		struct wmiiv_container *old_parent = window->pending.parent;
		window_detach(window);
		workspace_add_floating(workspace, window);
		view_set_tiled(window->view, false);
		if (window->view->using_csd) {
			window->saved_border = window->pending.border;
			window->pending.border = B_CSD;
			if (window->view->xdg_decoration) {
				struct wmiiv_xdg_decoration *deco = window->view->xdg_decoration;
				wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
						WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
			}
		}
		window_floating_set_default_size(window);
		window_floating_resize_and_center(window);
		if (old_parent) {
			column_consider_destroy(old_parent);
		}
	} else {
		// Returning to tiled
		window_detach(window);
		struct wmiiv_container *reference =
			seat_get_focus_inactive_tiling(seat, workspace);
		if (reference) {
			if (reference->view) {
				column_add_sibling(reference, window, 1);
			} else {
				column_add_child(reference, window);
			}
			window->pending.width = reference->pending.width;
			window->pending.height = reference->pending.height;
		} else {
			struct wmiiv_container *other =
				workspace_add_tiling(workspace, window);
			other->pending.width = workspace->width;
			other->pending.height = workspace->height;
		}
		if (window->view) {
			view_set_tiled(window->view, true);
			if (window->view->using_csd) {
				window->pending.border = window->saved_border;
				if (window->view->xdg_decoration) {
					struct wmiiv_xdg_decoration *deco = window->view->xdg_decoration;
					wlr_xdg_toplevel_decoration_v1_set_mode(deco->wlr_xdg_decoration,
							WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
				}
			}
		}
		window->width_fraction = 0;
		window->height_fraction = 0;
	}

	container_end_mouse_operation(window);

	ipc_event_window(window, "floating");
}

bool window_is_fullscreen(struct wmiiv_container* window) {
	if (!wmiiv_assert(container_is_window(window), "Only windows can be fullscreen")) {
		return false;
	}

	return window->pending.fullscreen_mode;
}

bool window_is_tiling(struct wmiiv_container* window) {
	if (!wmiiv_assert(container_is_window(window), "Only windows can be tiling")) {
		return false;
	}

	return window->pending.parent != NULL;
}

/**
 * Ensures all seats focus the fullscreen container if needed.
 */
static void workspace_focus_fullscreen(struct wmiiv_workspace *workspace) {
	// TODO (wmiiv) move me.
	if (!workspace->fullscreen) {
		return;
	}
	struct wmiiv_seat *seat;
	struct wmiiv_workspace *focus_workspace;
	wl_list_for_each(seat, &server.input->seats, link) {
		focus_workspace = seat_get_focused_workspace(seat);
		if (focus_workspace == workspace) {
			struct wmiiv_node *new_focus =
				seat_get_focus_inactive(seat, &workspace->fullscreen->node);
			seat_set_raw_focus(seat, new_focus);
		}
	}
}

static void window_move_to_column_from_maybe_direction(
		struct wmiiv_container *window, struct wmiiv_container *column,
		bool has_move_dir, enum wlr_direction move_dir) {
	if (!wmiiv_assert(container_is_window(window), "Can only move windows to columns")) {
		return;
	}
	if (!wmiiv_assert(container_is_column(column), "Can only move windows to columns")) {
		return;
	}

	if (window->pending.parent == column) {
		return;
	}

	struct wmiiv_seat *seat = input_manager_get_default_seat();
	struct wmiiv_workspace *old_workspace = window->pending.workspace;

	if (has_move_dir && (move_dir == WLR_DIRECTION_UP || move_dir == WLR_DIRECTION_DOWN)) {
		wmiiv_log(WMIIV_DEBUG, "Reparenting window (parallel)");
		int index =
			move_dir == WLR_DIRECTION_DOWN ?
			0 : column->pending.children->length;
		window_detach(window);
		column_insert_child(column, window, index);
		window->pending.width = window->pending.height = 0;
		window->width_fraction = window->height_fraction = 0;
	} else {
		wmiiv_log(WMIIV_DEBUG, "Reparenting window (perpendicular)");
		struct wmiiv_container *target_sibling = seat_get_focus_inactive_view(seat, &column->node);
		window_detach(window);
		if (target_sibling) {
			column_add_sibling(target_sibling, window, 1);
		} else {
			column_add_child(column, window);
		}
	}

	ipc_event_window(window, "move");

	if (column->pending.workspace) {
		workspace_focus_fullscreen(column->pending.workspace);
		workspace_detect_urgent(column->pending.workspace);
	}

	if (old_workspace && old_workspace != column->pending.workspace) {
		workspace_detect_urgent(old_workspace);
	}
}

void window_move_to_column_from_direction(
		struct wmiiv_container *window, struct wmiiv_container *column,
		enum wlr_direction move_dir) {
	window_move_to_column_from_maybe_direction(window, column, true, move_dir);
}

void window_move_to_column(struct wmiiv_container *window,
		struct wmiiv_container *column) {
	window_move_to_column_from_maybe_direction(window, column, false, WLR_DIRECTION_DOWN);
}

static void window_move_to_workspace_from_maybe_direction(
		struct wmiiv_container *window, struct wmiiv_workspace *workspace,
		bool has_move_dir, enum wlr_direction move_dir) {
	if (!wmiiv_assert(container_is_window(window), "Can only move windows between workspaces")) {
		return;
	}

	if (workspace == window->pending.workspace) {
		return;
	}

	struct wmiiv_seat *seat = input_manager_get_default_seat();

	// TODO (wmiiv) fullscreen.

	if (window_is_floating(window)) {
		struct wmiiv_output *old_output = window->pending.workspace->output;
		window_detach(window);
		workspace_add_floating(workspace, window);
		container_handle_fullscreen_reparent(window);
		// If changing output, center it within the workspace
		if (old_output != workspace->output && !window->pending.fullscreen_mode) {
			window_floating_move_to_center(window);
		}

		return;
	}

	window->pending.width = window->pending.height = 0;
	window->width_fraction = window->height_fraction = 0;

	if (!workspace->tiling->length) {
		struct wmiiv_container *column = column_create();
		workspace_insert_tiling_direct(workspace, column, 0);
	}

	if (has_move_dir && (move_dir == WLR_DIRECTION_LEFT || move_dir == WLR_DIRECTION_RIGHT)) {
		wmiiv_log(WMIIV_DEBUG, "Reparenting window (parallel)");
		// Move to either left-most or right-most column based on move
		// direction.
		int index =
			move_dir == WLR_DIRECTION_RIGHT ?
			0 : workspace->tiling->length;
		struct wmiiv_container *column = workspace->tiling->items[index];
		window_move_to_column_from_maybe_direction(window, column, has_move_dir, move_dir);
	} else {
		wmiiv_log(WMIIV_DEBUG, "Reparenting container (perpendicular)");
		// Move to the most recently focused column in the workspace.
		struct wmiiv_container *column = NULL;

		struct wmiiv_container *focus_inactive = seat_get_focus_inactive_tiling(seat, workspace);
		if (focus_inactive) {
			column = focus_inactive->pending.parent;
		} else {
			column = workspace->tiling->items[0];
		}
		window_move_to_column_from_maybe_direction(window, column, has_move_dir, move_dir);
	}
}

void window_move_to_workspace_from_direction(
		struct wmiiv_container *window, struct wmiiv_workspace *workspace,
		enum wlr_direction move_dir) {
	window_move_to_workspace_from_maybe_direction(window, workspace, true, move_dir);
}

void window_move_to_workspace(struct wmiiv_container *window,
		struct wmiiv_workspace *workspace) {
	window_move_to_workspace_from_maybe_direction(window, workspace, false, WLR_DIRECTION_DOWN);
}

struct wlr_surface *window_surface_at(struct wmiiv_container *window, double lx, double ly, double *sx, double *sy) {
	if (!wmiiv_assert(container_is_window(window), "Expected a view")) {
		return NULL;
	}
	struct wmiiv_view *view = window->view;
	double view_sx = lx - window->surface_x + view->geometry.x;
	double view_sy = ly - window->surface_y + view->geometry.y;

	double _sx, _sy;
	struct wlr_surface *surface = NULL;
	switch (view->type) {
#if HAVE_XWAYLAND
	case WMIIV_VIEW_XWAYLAND:
		surface = wlr_surface_surface_at(view->surface,
				view_sx, view_sy, &_sx, &_sy);
		break;
#endif
	case WMIIV_VIEW_XDG_SHELL:
		surface = wlr_xdg_surface_surface_at(
				view->wlr_xdg_toplevel->base,
				view_sx, view_sy, &_sx, &_sy);
		break;
	}
	if (surface) {
		*sx = _sx;
		*sy = _sy;
		return surface;
	}
	return NULL;
}

bool window_contents_contain_point(struct wmiiv_container *window, double lx, double ly) {
	if (!wmiiv_assert(container_is_window(window), "Expected a window")) {
		return false;
	}

	struct wlr_box box = {
		.x = window->pending.content_x,
		.y = window->pending.content_y,
		.width = window->pending.content_width,
		.height = window->pending.content_height,
	};

	return wlr_box_contains_point(&box, lx, ly);
}

bool window_contains_point(struct wmiiv_container *window, double lx, double ly) {
	if (!wmiiv_assert(container_is_window(window), "Expected a windowdwo")) {
		return false;
	}

	struct wlr_box box = {
		.x = window->pending.x,
		.y = window->pending.y,
		.width = window->pending.width,
		.height = window->pending.height,
	};

	return wlr_box_contains_point(&box, lx, ly);
}

struct wmiiv_container *window_obstructing_fullscreen_window(struct wmiiv_container *window)
{
	if (!wmiiv_assert(container_is_window(window), "Only windows can be fullscreen")) {
		return NULL;
	}

	struct wmiiv_workspace *workspace = window->pending.workspace;

	if (workspace && workspace->fullscreen && !window_is_fullscreen(window)) {
		if (container_is_transient_for(window, workspace->fullscreen)) {
			return NULL;
		}
		return workspace->fullscreen;
	}

	struct wmiiv_container *fullscreen_global = root->fullscreen_global;
	if (fullscreen_global && window != fullscreen_global) {
		if (container_is_transient_for(window, fullscreen_global)) {
			return NULL;
		}
		return fullscreen_global;
	}

	return NULL;
}

void window_damage_whole(struct wmiiv_container *window) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_whole_container(output, window);
	}
}

size_t window_titlebar_height(void) {
	return config->font_height + config->titlebar_v_padding * 2;
}

void floating_calculate_constraints(int *min_width, int *max_width,
		int *min_height, int *max_height) {
	if (config->floating_minimum_width == -1) { // no minimum
		*min_width = 0;
	} else if (config->floating_minimum_width == 0) { // automatic
		*min_width = 75;
	} else {
		*min_width = config->floating_minimum_width;
	}

	if (config->floating_minimum_height == -1) { // no minimum
		*min_height = 0;
	} else if (config->floating_minimum_height == 0) { // automatic
		*min_height = 50;
	} else {
		*min_height = config->floating_minimum_height;
	}

	struct wlr_box box;
	wlr_output_layout_get_box(root->output_layout, NULL, &box);

	if (config->floating_maximum_width == -1) { // no maximum
		*max_width = INT_MAX;
	} else if (config->floating_maximum_width == 0) { // automatic
		*max_width = box.width;
	} else {
		*max_width = config->floating_maximum_width;
	}

	if (config->floating_maximum_height == -1) { // no maximum
		*max_height = INT_MAX;
	} else if (config->floating_maximum_height == 0) { // automatic
		*max_height = box.height;
	} else {
		*max_height = config->floating_maximum_height;
	}

}

static void floating_natural_resize(struct wmiiv_container *window) {
	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	if (!window->view) {
		window->pending.width = fmax(min_width, fmin(window->pending.width, max_width));
		window->pending.height = fmax(min_height, fmin(window->pending.height, max_height));
	} else {
		struct wmiiv_view *view = window->view;
		window->pending.content_width =
			fmax(min_width, fmin(view->natural_width, max_width));
		window->pending.content_height =
			fmax(min_height, fmin(view->natural_height, max_height));
		window_set_geometry_from_content(window);
	}
}

void window_floating_resize_and_center(struct wmiiv_container *window) {
	struct wmiiv_workspace *workspace = window->pending.workspace;

	struct wlr_box ob;
	wlr_output_layout_get_box(root->output_layout, workspace->output->wlr_output, &ob);
	if (wlr_box_empty(&ob)) {
		// On NOOP output. Will be called again when moved to an output
		window->pending.x = 0;
		window->pending.y = 0;
		window->pending.width = 0;
		window->pending.height = 0;
		return;
	}

	floating_natural_resize(window);
	if (!window->view) {
		if (window->pending.width > workspace->width || window->pending.height > workspace->height) {
			window->pending.x = ob.x + (ob.width - window->pending.width) / 2;
			window->pending.y = ob.y + (ob.height - window->pending.height) / 2;
		} else {
			window->pending.x = workspace->x + (workspace->width - window->pending.width) / 2;
			window->pending.y = workspace->y + (workspace->height - window->pending.height) / 2;
		}
	} else {
		if (window->pending.content_width > workspace->width
				|| window->pending.content_height > workspace->height) {
			window->pending.content_x = ob.x + (ob.width - window->pending.content_width) / 2;
			window->pending.content_y = ob.y + (ob.height - window->pending.content_height) / 2;
		} else {
			window->pending.content_x = workspace->x + (workspace->width - window->pending.content_width) / 2;
			window->pending.content_y = workspace->y + (workspace->height - window->pending.content_height) / 2;
		}

		// If the view's border is B_NONE then these properties are ignored.
		window->pending.border_top = window->pending.border_bottom = true;
		window->pending.border_left = window->pending.border_right = true;

		window_set_geometry_from_content(window);
	}
}

void window_floating_set_default_size(struct wmiiv_container *window) {
	if (!wmiiv_assert(window->pending.workspace, "Expected a window on a workspace")) {
		return;
	}

	wmiiv_assert(container_is_window(window), "Expected window");

	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	struct wlr_box *box = calloc(1, sizeof(struct wlr_box));
	workspace_get_box(window->pending.workspace, box);

	double width = fmax(min_width, fmin(box->width * 0.5, max_width));
	double height = fmax(min_height, fmin(box->height * 0.75, max_height));

	window->pending.content_width = width;
	window->pending.content_height = height;
	window_set_geometry_from_content(window);

	free(box);
}

void window_floating_translate(struct wmiiv_container *window,
		double x_amount, double y_amount) {
	if (!wmiiv_assert(window_is_floating(window), "Expected a floating window")) {
		return;
	}
	window->pending.x += x_amount;
	window->pending.y += y_amount;
	window->pending.content_x += x_amount;
	window->pending.content_y += y_amount;

	node_set_dirty(&window->node);
}

/**
 * Choose an output for the floating window's new position.
 *
 * If the center of the window intersects an output then we'll choose that
 * one, otherwise we'll choose whichever output is closest to the window's
 * center.
 */
struct wmiiv_output *window_floating_find_output(struct wmiiv_container *window) {
	if (!wmiiv_assert(window_is_floating(window), "Expected a floating window")) {
		return NULL;
	}
	double center_x = window->pending.x + window->pending.width / 2;
	double center_y = window->pending.y + window->pending.height / 2;
	struct wmiiv_output *closest_output = NULL;
	double closest_distance = DBL_MAX;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		double closest_x, closest_y;
		output_get_box(output, &output_box);
		wlr_box_closest_point(&output_box, center_x, center_y,
				&closest_x, &closest_y);
		if (center_x == closest_x && center_y == closest_y) {
			// The center of the floating window is on this output
			return output;
		}
		double x_dist = closest_x - center_x;
		double y_dist = closest_y - center_y;
		double distance = x_dist * x_dist + y_dist * y_dist;
		if (distance < closest_distance) {
			closest_output = output;
			closest_distance = distance;
		}
	}
	return closest_output;
}

void window_floating_move_to(struct wmiiv_container *window,
		double lx, double ly) {
	if (!wmiiv_assert(window_is_floating(window), "Expected a floating window")) {
		return;
	}
	window_floating_translate(window, lx - window->pending.x, ly - window->pending.y);
	struct wmiiv_workspace *old_workspace = window->pending.workspace;
	struct wmiiv_output *new_output = window_floating_find_output(window);
	if (!wmiiv_assert(new_output, "Unable to find any output")) {
		return;
	}
	struct wmiiv_workspace *new_workspace =
		output_get_active_workspace(new_output);
	if (new_workspace && old_workspace != new_workspace) {
		window_detach(window);
		workspace_add_floating(new_workspace, window);
		arrange_workspace(old_workspace);
		arrange_workspace(new_workspace);
		workspace_detect_urgent(old_workspace);
		workspace_detect_urgent(new_workspace);
	}
}

void window_floating_move_to_center(struct wmiiv_container *window) {
	if (!wmiiv_assert(window_is_floating(window), "Expected a floating window")) {
		return;
	}
	struct wmiiv_workspace *workspace = window->pending.workspace;
	double new_lx = workspace->x + (workspace->width - window->pending.width) / 2;
	double new_ly = workspace->y + (workspace->height - window->pending.height) / 2;
	window_floating_translate(window, new_lx - window->pending.x, new_ly - window->pending.y);
}

void window_get_box(struct wmiiv_container *window, struct wlr_box *box) {
	box->x = window->pending.x;
	box->y = window->pending.y;
	box->width = window->pending.width;
	box->height = window->pending.height;
}

/**
 * Indicate to clients in this window that they are participating in (or
 * have just finished) an interactive resize
 */
void window_set_resizing(struct wmiiv_container *window, bool resizing) {
	if (!window) {
		return;
	}

	wmiiv_assert(container_is_window(window), "Expected window");

	if (window->view->impl->set_resizing) {
		window->view->impl->set_resizing(window->view, resizing);
	}
}

void window_set_geometry_from_content(struct wmiiv_container *window) {
	if (!wmiiv_assert(window->view, "Expected a view")) {
		return;
	}
	if (!wmiiv_assert(window_is_floating(window), "Expected a floating view")) {
		return;
	}
	size_t border_width = 0;
	size_t top = 0;

	if (window->pending.border != B_CSD && !window->pending.fullscreen_mode) {
		border_width = window->pending.border_thickness * (window->pending.border != B_NONE);
		top = window->pending.border == B_NORMAL ?
			window_titlebar_height() : border_width;
	}

	window->pending.x = window->pending.content_x - border_width;
	window->pending.y = window->pending.content_y - top;
	window->pending.width = window->pending.content_width + border_width * 2;
	window->pending.height = top + window->pending.content_height + border_width;
	node_set_dirty(&window->node);
}
