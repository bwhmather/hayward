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

bool container_is_column(struct wmiiv_container* container) {
	return container->view == NULL;
}

bool container_is_window(struct wmiiv_container* container) {
	return container->view != NULL;
}

void container_destroy(struct wmiiv_container *container) {
	if (!wmiiv_assert(container->node.destroying,
				"Tried to free container which wasn't marked as destroying")) {
		return;
	}
	if (!wmiiv_assert(container->node.ntxnrefs == 0, "Tried to free container "
				"which is still referenced by transactions")) {
		return;
	}
	free(container->title);
	free(container->formatted_title);
	wlr_texture_destroy(container->title_focused);
	wlr_texture_destroy(container->title_focused_inactive);
	wlr_texture_destroy(container->title_unfocused);
	wlr_texture_destroy(container->title_urgent);
	wlr_texture_destroy(container->title_focused_tab_title);
	list_free(container->pending.children);
	list_free(container->current.children);
	list_free(container->outputs);

	list_free_items_and_destroy(container->marks);
	wlr_texture_destroy(container->marks_focused);
	wlr_texture_destroy(container->marks_focused_inactive);
	wlr_texture_destroy(container->marks_unfocused);
	wlr_texture_destroy(container->marks_urgent);
	wlr_texture_destroy(container->marks_focused_tab_title);

	if (container->view && container->view->container == container) {
		container->view->container = NULL;
		if (container->view->destroying) {
			view_destroy(container->view);
		}
	}

	free(container);
}

void container_begin_destroy(struct wmiiv_container *container) {
	if (container->view) {
		ipc_event_window(container, "close");
	}
	// The workspace must have the fullscreen pointer cleared so that the
	// seat code can find an appropriate new focus.
	if (container->pending.fullscreen_mode == FULLSCREEN_WORKSPACE && container->pending.workspace) {
		container->pending.workspace->fullscreen = NULL;
	}

	wl_signal_emit(&container->node.events.destroy, &container->node);

	container_end_mouse_operation(container);

	container->node.destroying = true;
	node_set_dirty(&container->node);

	if (container->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		container_fullscreen_disable(container);
	}

	if (container->pending.parent || container->pending.workspace) {
		if (container_is_window(container)) {
			window_detach(container);
		} else {
			column_detach(container);
		}
	}
}

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct wmiiv_output *container_get_effective_output(struct wmiiv_container *container) {
	if (container->outputs->length == 0) {
		return NULL;
	}
	return container->outputs->items[container->outputs->length - 1];
}

static void render_titlebar_text_texture(struct wmiiv_output *output,
		struct wmiiv_container *container, struct wlr_texture **texture,
		struct border_colors *class, bool pango_markup, char *text) {
	// TODO (wmiiv) duplicated in in window. remove once columns stop rendering titles.
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

static void update_title_texture(struct wmiiv_container *container,
		struct wlr_texture **texture, struct border_colors *class) {
	struct wmiiv_output *output = container_get_effective_output(container);
	if (!output) {
		return;
	}
	if (*texture) {
		wlr_texture_destroy(*texture);
		*texture = NULL;
	}
	if (!container->formatted_title) {
		return;
	}

	render_titlebar_text_texture(output, container, texture, class,
		config->pango_markup, container->formatted_title);
}

void container_update_title_textures(struct wmiiv_container *container) {
	update_title_texture(container, &container->title_focused,
			&config->border_colors.focused);
	update_title_texture(container, &container->title_focused_inactive,
			&config->border_colors.focused_inactive);
	update_title_texture(container, &container->title_unfocused,
			&config->border_colors.unfocused);
	update_title_texture(container, &container->title_urgent,
			&config->border_colors.urgent);
	update_title_texture(container, &container->title_focused_tab_title,
			&config->border_colors.focused_tab_title);
	if (container_is_window(container)) {
		window_damage_whole(container);
	} else {
		column_damage_whole(container);
	}
}

/**
 * Calculate and return the length of the tree representation.
 * An example tree representation is: V[Terminal, Firefox]
 * If buffer is not NULL, also populate the buffer with the representation.
 */
size_t container_build_representation(enum wmiiv_container_layout layout,
		list_t *children, char *buffer) {
	size_t len = 2;
	switch (layout) {
	case L_VERT:
		lenient_strcat(buffer, "V[");
		break;
	case L_HORIZ:
		lenient_strcat(buffer, "H[");
		break;
	case L_TABBED:
		lenient_strcat(buffer, "T[");
		break;
	case L_STACKED:
		lenient_strcat(buffer, "S[");
		break;
	case L_NONE:
		lenient_strcat(buffer, "D[");
		break;
	}
	for (int i = 0; i < children->length; ++i) {
		if (i != 0) {
			++len;
			lenient_strcat(buffer, " ");
		}
		struct wmiiv_container *child = children->items[i];
		const char *identifier = NULL;
		if (child->view) {
			identifier = view_get_class(child->view);
			if (!identifier) {
				identifier = view_get_app_id(child->view);
			}
		} else {
			identifier = child->formatted_title;
		}
		if (identifier) {
			len += strlen(identifier);
			lenient_strcat(buffer, identifier);
		} else {
			len += 6;
			lenient_strcat(buffer, "(null)");
		}
	}
	++len;
	lenient_strcat(buffer, "]");
	return len;
}

void container_update_representation(struct wmiiv_container *container) {
	if (!container->view) {
		size_t len = container_build_representation(container->pending.layout,
				container->pending.children, NULL);
		free(container->formatted_title);
		container->formatted_title = calloc(len + 1, sizeof(char));
		if (!wmiiv_assert(container->formatted_title,
					"Unable to allocate title string")) {
			return;
		}
		container_build_representation(container->pending.layout, container->pending.children,
				container->formatted_title);
		container_update_title_textures(container);
	}
	if (container->pending.parent) {
		container_update_representation(container->pending.parent);
	} else if (container->pending.workspace) {
		workspace_update_representation(container->pending.workspace);
	}
}

size_t container_titlebar_height(void) {
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

static void floating_natural_resize(struct wmiiv_container *container) {
	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	if (!container->view) {
		container->pending.width = fmax(min_width, fmin(container->pending.width, max_width));
		container->pending.height = fmax(min_height, fmin(container->pending.height, max_height));
	} else {
		struct wmiiv_view *view = container->view;
		container->pending.content_width =
			fmax(min_width, fmin(view->natural_width, max_width));
		container->pending.content_height =
			fmax(min_height, fmin(view->natural_height, max_height));
		container_set_geometry_from_content(container);
	}
}

void container_floating_resize_and_center(struct wmiiv_container *container) {
	struct wmiiv_workspace *workspace = container->pending.workspace;

	struct wlr_box ob;
	wlr_output_layout_get_box(root->output_layout, workspace->output->wlr_output, &ob);
	if (wlr_box_empty(&ob)) {
		// On NOOP output. Will be called again when moved to an output
		container->pending.x = 0;
		container->pending.y = 0;
		container->pending.width = 0;
		container->pending.height = 0;
		return;
	}

	floating_natural_resize(container);
	if (!container->view) {
		if (container->pending.width > workspace->width || container->pending.height > workspace->height) {
			container->pending.x = ob.x + (ob.width - container->pending.width) / 2;
			container->pending.y = ob.y + (ob.height - container->pending.height) / 2;
		} else {
			container->pending.x = workspace->x + (workspace->width - container->pending.width) / 2;
			container->pending.y = workspace->y + (workspace->height - container->pending.height) / 2;
		}
	} else {
		if (container->pending.content_width > workspace->width
				|| container->pending.content_height > workspace->height) {
			container->pending.content_x = ob.x + (ob.width - container->pending.content_width) / 2;
			container->pending.content_y = ob.y + (ob.height - container->pending.content_height) / 2;
		} else {
			container->pending.content_x = workspace->x + (workspace->width - container->pending.content_width) / 2;
			container->pending.content_y = workspace->y + (workspace->height - container->pending.content_height) / 2;
		}

		// If the view's border is B_NONE then these properties are ignored.
		container->pending.border_top = container->pending.border_bottom = true;
		container->pending.border_left = container->pending.border_right = true;

		container_set_geometry_from_content(container);
	}
}

void container_floating_set_default_size(struct wmiiv_container *container) {
	if (!wmiiv_assert(container->pending.workspace, "Expected a container on a workspace")) {
		return;
	}

	int min_width, max_width, min_height, max_height;
	floating_calculate_constraints(&min_width, &max_width,
			&min_height, &max_height);
	struct wlr_box *box = calloc(1, sizeof(struct wlr_box));
	workspace_get_box(container->pending.workspace, box);

	double width = fmax(min_width, fmin(box->width * 0.5, max_width));
	double height = fmax(min_height, fmin(box->height * 0.75, max_height));
	if (!container->view) {
		container->pending.width = width;
		container->pending.height = height;
	} else {
		container->pending.content_width = width;
		container->pending.content_height = height;
		container_set_geometry_from_content(container);
	}

	free(box);
}


/**
 * Indicate to clients in this container that they are participating in (or
 * have just finished) an interactive resize
 */
void container_set_resizing(struct wmiiv_container *container, bool resizing) {
	if (!container) {
		return;
	}

	if (container->view) {
		if (container->view->impl->set_resizing) {
			container->view->impl->set_resizing(container->view, resizing);
		}
	} else {
		for (int i = 0; i < container->pending.children->length; ++i ) {
			struct wmiiv_container *child = container->pending.children->items[i];
			container_set_resizing(child, resizing);
		}
	}
}

void container_set_geometry_from_content(struct wmiiv_container *container) {
	if (!wmiiv_assert(container->view, "Expected a view")) {
		return;
	}
	if (!wmiiv_assert(window_is_floating(container), "Expected a floating view")) {
		return;
	}
	size_t border_width = 0;
	size_t top = 0;

	if (container->pending.border != B_CSD && !container->pending.fullscreen_mode) {
		border_width = container->pending.border_thickness * (container->pending.border != B_NONE);
		top = container->pending.border == B_NORMAL ?
			container_titlebar_height() : border_width;
	}

	container->pending.x = container->pending.content_x - border_width;
	container->pending.y = container->pending.content_y - top;
	container->pending.width = container->pending.content_width + border_width * 2;
	container->pending.height = top + container->pending.content_height + border_width;
	node_set_dirty(&container->node);
}

void container_get_box(struct wmiiv_container *container, struct wlr_box *box) {
	box->x = container->pending.x;
	box->y = container->pending.y;
	box->width = container->pending.width;
	box->height = container->pending.height;
}

/**
 * Translate the container's position as well as all children.
 */
void container_floating_translate(struct wmiiv_container *container,
		double x_amount, double y_amount) {
	container->pending.x += x_amount;
	container->pending.y += y_amount;
	container->pending.content_x += x_amount;
	container->pending.content_y += y_amount;

	if (container->pending.children) {
		for (int i = 0; i < container->pending.children->length; ++i) {
			struct wmiiv_container *child = container->pending.children->items[i];
			container_floating_translate(child, x_amount, y_amount);
		}
	}

	node_set_dirty(&container->node);
}

/**
 * Choose an output for the floating container's new position.
 *
 * If the center of the container intersects an output then we'll choose that
 * one, otherwise we'll choose whichever output is closest to the container's
 * center.
 */
struct wmiiv_output *container_floating_find_output(struct wmiiv_container *container) {
	double center_x = container->pending.x + container->pending.width / 2;
	double center_y = container->pending.y + container->pending.height / 2;
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
			// The center of the floating container is on this output
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

void container_floating_move_to(struct wmiiv_container *window,
		double lx, double ly) {
	if (!wmiiv_assert(window_is_floating(window),
			"Expected a floating container")) {
		return;
	}
	container_floating_translate(window, lx - window->pending.x, ly - window->pending.y);
	struct wmiiv_workspace *old_workspace = window->pending.workspace;
	struct wmiiv_output *new_output = container_floating_find_output(window);
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

void container_floating_move_to_center(struct wmiiv_container *container) {
	if (!wmiiv_assert(window_is_floating(container),
			"Expected a floating container")) {
		return;
	}
	struct wmiiv_workspace *workspace = container->pending.workspace;
	double new_lx = workspace->x + (workspace->width - container->pending.width) / 2;
	double new_ly = workspace->y + (workspace->height - container->pending.height) / 2;
	container_floating_translate(container, new_lx - container->pending.x, new_ly - container->pending.y);
}

static bool find_urgent_iterator(struct wmiiv_container *container, void *data) {
	return container->view && view_is_urgent(container->view);
}

bool container_has_urgent_child(struct wmiiv_container *container) {
	return column_find_child(container, find_urgent_iterator, NULL);
}

void container_end_mouse_operation(struct wmiiv_container *container) {
	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seatop_unref(seat, container);
	}
}

static bool devid_from_fd(int fd, dev_t *devid) {
	struct stat stat;
	if (fstat(fd, &stat) != 0) {
		wmiiv_log_errno(WMIIV_ERROR, "fstat failed");
		return false;
	}
	*devid = stat.st_rdev;
	return true;
}

static void set_fullscreen(struct wmiiv_container *container, bool enable) {
	if (!container->view) {
		return;
	}
	if (container->view->impl->set_fullscreen) {
		container->view->impl->set_fullscreen(container->view, enable);
		if (container->view->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_fullscreen(
				container->view->foreign_toplevel, enable);
		}
	}

	if (!server.linux_dmabuf_v1 || !container->view->surface) {
		return;
	}
	if (!enable) {
		wlr_linux_dmabuf_v1_set_surface_feedback(server.linux_dmabuf_v1,
			container->view->surface, NULL);
		return;
	}

	if (!container->pending.workspace || !container->pending.workspace->output) {
		return;
	}

	struct wmiiv_output *output = container->pending.workspace->output;
	struct wlr_output *wlr_output = output->wlr_output;

	// TODO: add wlroots helpers for all of this stuff

	const struct wlr_drm_format_set *renderer_formats =
		wlr_renderer_get_dmabuf_texture_formats(server.renderer);
	assert(renderer_formats);

	int renderer_drm_fd = wlr_renderer_get_drm_fd(server.renderer);
	int backend_drm_fd = wlr_backend_get_drm_fd(wlr_output->backend);
	if (renderer_drm_fd < 0 || backend_drm_fd < 0) {
		return;
	}

	dev_t render_dev, scanout_dev;
	if (!devid_from_fd(renderer_drm_fd, &render_dev) ||
			!devid_from_fd(backend_drm_fd, &scanout_dev)) {
		return;
	}

	const struct wlr_drm_format_set *output_formats =
		wlr_output_get_primary_formats(output->wlr_output,
		WLR_BUFFER_CAP_DMABUF);
	if (!output_formats) {
		return;
	}

	struct wlr_drm_format_set scanout_formats = {0};
	if (!wlr_drm_format_set_intersect(&scanout_formats,
			output_formats, renderer_formats)) {
		return;
	}

	struct wlr_linux_dmabuf_feedback_v1_tranche tranches[] = {
		{
			.target_device = scanout_dev,
			.flags = ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT,
			.formats = &scanout_formats,
		},
		{
			.target_device = render_dev,
			.formats = renderer_formats,
		},
	};

	const struct wlr_linux_dmabuf_feedback_v1 feedback = {
		.main_device = render_dev,
		.tranches = tranches,
		.tranches_len = sizeof(tranches) / sizeof(tranches[0]),
	};
	wlr_linux_dmabuf_v1_set_surface_feedback(server.linux_dmabuf_v1,
		container->view->surface, &feedback);

	wlr_drm_format_set_finish(&scanout_formats);
}

static void container_fullscreen_workspace(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return;
	}
	if (!wmiiv_assert(window->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	set_fullscreen(window, true);
	window->pending.fullscreen_mode = FULLSCREEN_WORKSPACE;

	window->saved_x = window->pending.x;
	window->saved_y = window->pending.y;
	window->saved_width = window->pending.width;
	window->saved_height = window->pending.height;

	if (window->pending.workspace) {
		window->pending.workspace->fullscreen = window;
		struct wmiiv_seat *seat;
		struct wmiiv_workspace *focus_workspace;
		wl_list_for_each(seat, &server.input->seats, link) {
			focus_workspace = seat_get_focused_workspace(seat);
			if (focus_workspace == window->pending.workspace) {
				seat_set_focus_window(seat, window);
			} else {
				struct wmiiv_node *focus =
					seat_get_focus_inactive(seat, &root->node);
				seat_set_raw_focus(seat, &window->node);
				seat_set_raw_focus(seat, focus);
			}
		}
	}

	container_end_mouse_operation(window);
	ipc_event_window(window, "fullscreen_mode");
}

static void container_fullscreen_global(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return;
	}
	if (!wmiiv_assert(window->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	set_fullscreen(window, true);

	root->fullscreen_global = window;
	window->saved_x = window->pending.x;
	window->saved_y = window->pending.y;
	window->saved_width = window->pending.width;
	window->saved_height = window->pending.height;

	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct wmiiv_container *focus = seat_get_focused_container(seat);
		if (focus && focus != window) {
			seat_set_focus_window(seat, window);
		}
	}

	window->pending.fullscreen_mode = FULLSCREEN_GLOBAL;
	container_end_mouse_operation(window);
	ipc_event_window(window, "fullscreen_mode");
}

void container_fullscreen_disable(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return;
	}
	if (!wmiiv_assert(window->pending.fullscreen_mode != FULLSCREEN_NONE,
				"Expected a fullscreen container")) {
		return;
	}
	set_fullscreen(window, false);

	if (window_is_floating(window)) {
		window->pending.x = window->saved_x;
		window->pending.y = window->saved_y;
		window->pending.width = window->saved_width;
		window->pending.height = window->saved_height;
	}

	if (window->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		if (window->pending.workspace) {
			window->pending.workspace->fullscreen = NULL;
			if (window_is_floating(window)) {
				struct wmiiv_output *output =
					container_floating_find_output(window);
				if (window->pending.workspace->output != output) {
					container_floating_move_to_center(window);
				}
			}
		}
	} else {
		root->fullscreen_global = NULL;
	}

	// If the container was mapped as fullscreen and set as floating by
	// criteria, it needs to be reinitialized as floating to get the proper
	// size and location
	if (window_is_floating(window) && (window->pending.width == 0 || window->pending.height == 0)) {
		container_floating_resize_and_center(window);
	}

	window->pending.fullscreen_mode = FULLSCREEN_NONE;
	container_end_mouse_operation(window);
	ipc_event_window(window, "fullscreen_mode");
}

void container_set_fullscreen(struct wmiiv_container *container,
		enum wmiiv_fullscreen_mode mode) {
	if (container->pending.fullscreen_mode == mode) {
		return;
	}

	switch (mode) {
	case FULLSCREEN_NONE:
		container_fullscreen_disable(container);
		break;
	case FULLSCREEN_WORKSPACE:
		// TODO (wmiiv) if disabling previous fullscreen window is
		// neccessary, why are these disable/enable functions public
		// and non-static.
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (container->pending.workspace && container->pending.workspace->fullscreen) {
			container_fullscreen_disable(container->pending.workspace->fullscreen);
		}
		container_fullscreen_workspace(container);
		break;
	case FULLSCREEN_GLOBAL:
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (container->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
			container_fullscreen_disable(container);
		}
		container_fullscreen_global(container);
		break;
	}
}

struct wmiiv_container *container_toplevel_ancestor(
		struct wmiiv_container *container) {
	while (container->pending.parent) {
		container = container->pending.parent;
	}

	return container;
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_leave(surface, wlr_output);
}

void container_discover_outputs(struct wmiiv_container *container) {
	struct wlr_box container_box = {
		.x = container->current.x,
		.y = container->current.y,
		.width = container->current.width,
		.height = container->current.height,
	};
	struct wmiiv_output *old_output = container_get_effective_output(container);

	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		output_get_box(output, &output_box);
		struct wlr_box intersection;
		bool intersects =
			wlr_box_intersection(&intersection, &container_box, &output_box);
		int index = list_find(container->outputs, output);

		if (intersects && index == -1) {
			// Send enter
			wmiiv_log(WMIIV_DEBUG, "Container %p entered output %p", container, output);
			if (container->view) {
				view_for_each_surface(container->view,
						surface_send_enter_iterator, output->wlr_output);
				if (container->view->foreign_toplevel) {
					wlr_foreign_toplevel_handle_v1_output_enter(
							container->view->foreign_toplevel, output->wlr_output);
				}
			}
			list_add(container->outputs, output);
		} else if (!intersects && index != -1) {
			// Send leave
			wmiiv_log(WMIIV_DEBUG, "Container %p left output %p", container, output);
			if (container->view) {
				view_for_each_surface(container->view,
					surface_send_leave_iterator, output->wlr_output);
				if (container->view->foreign_toplevel) {
					wlr_foreign_toplevel_handle_v1_output_leave(
							container->view->foreign_toplevel, output->wlr_output);
				}
			}
			list_del(container->outputs, index);
		}
	}
	struct wmiiv_output *new_output = container_get_effective_output(container);
	double old_scale = old_output && old_output->enabled ?
		old_output->wlr_output->scale : -1;
	double new_scale = new_output ? new_output->wlr_output->scale : -1;
	if (old_scale != new_scale) {
		container_update_title_textures(container);
		if (container_is_window(container)) {
			window_update_marks_textures(container);
		}
	}
}

enum wmiiv_container_layout container_parent_layout(struct wmiiv_container *container) {
	if (container_is_window(container)) {
		if (container->pending.parent) {
			return container->pending.parent->pending.layout;
		}
		return L_NONE;
	} else {
		// TODO (wmiiv) There should be no need for this branch.  Can
		// probably all be moved to window module.
		if (container->pending.parent) {
			return container->pending.parent->pending.layout;
		}
		if (container->pending.workspace) {
			return L_HORIZ;
		}
		return L_NONE;
	}
}

enum wmiiv_container_layout container_current_parent_layout(
		struct wmiiv_container *container) {
	if (container->current.parent) {
		return container->current.parent->current.layout;
	}
	// TODO (wmiiv) workspace default layout.
	return L_HORIZ;
}

list_t *container_get_siblings(struct wmiiv_container *container) {
	if (container->pending.parent) {
		return container->pending.parent->pending.children;
	}
	if (list_find(container->pending.workspace->tiling, container) != -1) {
		return container->pending.workspace->tiling;
	}
	return container->pending.workspace->floating;
}

int container_sibling_index(struct wmiiv_container *child) {
	return list_find(container_get_siblings(child), child);
}

list_t *container_get_current_siblings(struct wmiiv_container *container) {
	if (container->current.parent) {
		return container->current.parent->current.children;
	}
	return container->current.workspace->current.tiling;
}

void container_handle_fullscreen_reparent(struct wmiiv_container *container) {
	if (container->pending.fullscreen_mode != FULLSCREEN_WORKSPACE || !container->pending.workspace ||
			container->pending.workspace->fullscreen == container) {
		return;
	}
	if (container->pending.workspace->fullscreen) {
		container_fullscreen_disable(container->pending.workspace->fullscreen);
	}
	container->pending.workspace->fullscreen = container;

	arrange_workspace(container->pending.workspace);
}

bool container_is_transient_for(struct wmiiv_container *child,
		struct wmiiv_container *ancestor) {
	return config->popup_during_fullscreen == POPUP_SMART &&
		child->view && ancestor->view &&
		view_is_transient_for(child->view, ancestor->view);
}

void container_raise_floating(struct wmiiv_container *window) {
	// Bring container to front by putting it at the end of the floating list.
	if (window_is_floating(window) && window->pending.workspace) {
		list_move_to_end(window->pending.workspace->floating, window);
		node_set_dirty(&window->pending.workspace->node);
	}
}

bool container_is_sticky(struct wmiiv_container *container) {
	return container_is_window(container) && container->is_sticky && window_is_floating(container);
}

bool container_is_sticky_or_child(struct wmiiv_container *container) {
	return container_is_sticky(container_toplevel_ancestor(container));
}
