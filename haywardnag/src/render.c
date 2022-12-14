#include <stdint.h>
#include "hayward-common/cairo_util.h"
#include "hayward-common/log.h"
#include "hayward-common/pango.h"
#include "hayward-client/pool-buffer.h"
#include "haywardnag/haywardnag.h"
#include "haywardnag/types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static uint32_t render_message(cairo_t *cairo, struct haywardnag *haywardnag) {
	int text_width, text_height;
	get_text_size(cairo, haywardnag->type->font, &text_width, &text_height, NULL,
			1, true, "%s", haywardnag->message);

	int padding = haywardnag->type->message_padding;

	uint32_t ideal_height = text_height + padding * 2;
	uint32_t ideal_surface_height = ideal_height;
	if (haywardnag->height < ideal_surface_height) {
		return ideal_surface_height;
	}

	cairo_set_source_u32(cairo, haywardnag->type->text);
	cairo_move_to(cairo, padding, (int)(ideal_height - text_height) / 2);
	render_text(cairo, haywardnag->type->font, 1, false,
			"%s", haywardnag->message);

	return ideal_surface_height;
}

static void render_details_scroll_button(cairo_t *cairo,
		struct haywardnag *haywardnag, struct haywardnag_button *button) {
	int text_width, text_height;
	get_text_size(cairo, haywardnag->type->font, &text_width, &text_height, NULL,
			1, true, "%s", button->text);

	int border = haywardnag->type->button_border_thickness;
	int padding = haywardnag->type->button_padding;

	cairo_set_source_u32(cairo, haywardnag->type->details_background);
	cairo_rectangle(cairo, button->x, button->y,
			button->width, button->height);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, haywardnag->type->button_background);
	cairo_rectangle(cairo, button->x + border, button->y + border,
			button->width - (border * 2), button->height - (border * 2));
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, haywardnag->type->button_text);
	cairo_move_to(cairo, button->x + border + padding,
			button->y + border + (button->height - text_height) / 2);
	render_text(cairo, haywardnag->type->font, 1, true,
			"%s", button->text);
}

static int get_detailed_scroll_button_width(cairo_t *cairo,
		struct haywardnag *haywardnag) {
	int up_width, down_width, temp_height;
	get_text_size(cairo, haywardnag->type->font, &up_width, &temp_height, NULL,
			1, true,
			"%s", haywardnag->details.button_up.text);
	get_text_size(cairo, haywardnag->type->font, &down_width, &temp_height, NULL,
			1, true,
			"%s", haywardnag->details.button_down.text);

	int text_width =  up_width > down_width ? up_width : down_width;
	int border = haywardnag->type->button_border_thickness;
	int padding = haywardnag->type->button_padding;

	return text_width + border * 2 + padding * 2;
}

static uint32_t render_detailed(cairo_t *cairo, struct haywardnag *haywardnag,
		uint32_t y) {
	uint32_t width = haywardnag->width;

	int border = haywardnag->type->details_border_thickness;
	int padding = haywardnag->type->message_padding;
	int decor = padding + border;

	haywardnag->details.x = decor;
	haywardnag->details.y = y + decor;
	haywardnag->details.width = width - decor * 2;

	PangoLayout *layout = get_pango_layout(cairo, haywardnag->type->font,
			haywardnag->details.message, 1, false);
	pango_layout_set_width(layout,
			(haywardnag->details.width - padding * 2) * PANGO_SCALE);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_single_paragraph_mode(layout, false);
	pango_cairo_update_layout(cairo, layout);
	haywardnag->details.total_lines = pango_layout_get_line_count(layout);

	PangoLayoutLine *line;
	line = pango_layout_get_line_readonly(layout, haywardnag->details.offset);
	gint offset = line->start_index;
	const char *text = pango_layout_get_text(layout);
	pango_layout_set_text(layout, text + offset, strlen(text) - offset);

	int text_width, text_height;
	pango_cairo_update_layout(cairo, layout);
	pango_layout_get_pixel_size(layout, &text_width, &text_height);

	bool show_buttons = haywardnag->details.offset > 0;
	int button_width = get_detailed_scroll_button_width(cairo, haywardnag);
	if (show_buttons) {
		haywardnag->details.width -= button_width;
		pango_layout_set_width(layout,
				(haywardnag->details.width - padding * 2) * PANGO_SCALE);
	}

	uint32_t ideal_height;
	do {
		ideal_height = haywardnag->details.y + text_height + decor + padding * 2;
		if (ideal_height > HAYWARDNAG_MAX_HEIGHT) {
			ideal_height = HAYWARDNAG_MAX_HEIGHT;

			if (!show_buttons) {
				show_buttons = true;
				haywardnag->details.width -= button_width;
				pango_layout_set_width(layout,
						(haywardnag->details.width - padding * 2) * PANGO_SCALE);
			}
		}

		haywardnag->details.height = ideal_height - haywardnag->details.y - decor;
		pango_layout_set_height(layout,
				(haywardnag->details.height - padding * 2) * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_cairo_update_layout(cairo, layout);
		pango_layout_get_pixel_size(layout, &text_width, &text_height);
	} while (text_height != (haywardnag->details.height - padding * 2));

	haywardnag->details.visible_lines = pango_layout_get_line_count(layout);

	if (show_buttons) {
		haywardnag->details.button_up.x =
			haywardnag->details.x + haywardnag->details.width;
		haywardnag->details.button_up.y = haywardnag->details.y;
		haywardnag->details.button_up.width = button_width;
		haywardnag->details.button_up.height = haywardnag->details.height / 2;
		render_details_scroll_button(cairo, haywardnag,
				&haywardnag->details.button_up);

		haywardnag->details.button_down.x =
			haywardnag->details.x + haywardnag->details.width;
		haywardnag->details.button_down.y =
			haywardnag->details.button_up.y + haywardnag->details.button_up.height;
		haywardnag->details.button_down.width = button_width;
		haywardnag->details.button_down.height = haywardnag->details.height / 2;
		render_details_scroll_button(cairo, haywardnag,
				&haywardnag->details.button_down);
	}

	cairo_set_source_u32(cairo, haywardnag->type->details_background);
	cairo_rectangle(cairo, haywardnag->details.x, haywardnag->details.y,
			haywardnag->details.width, haywardnag->details.height);
	cairo_fill(cairo);

	cairo_move_to(cairo, haywardnag->details.x + padding,
			haywardnag->details.y + padding);
	cairo_set_source_u32(cairo, haywardnag->type->text);
	pango_cairo_show_layout(cairo, layout);
	g_object_unref(layout);

	return ideal_height;
}

static uint32_t render_button(cairo_t *cairo, struct haywardnag *haywardnag,
		int button_index, int *x) {
	struct haywardnag_button *button = haywardnag->buttons->items[button_index];

	int text_width, text_height;
	get_text_size(cairo, haywardnag->type->font, &text_width, &text_height, NULL,
			1, true, "%s", button->text);

	int border = haywardnag->type->button_border_thickness;
	int padding = haywardnag->type->button_padding;

	uint32_t ideal_height = text_height + padding * 2 + border * 2;
	uint32_t ideal_surface_height = ideal_height;
	if (haywardnag->height < ideal_surface_height) {
		return ideal_surface_height;
	}

	button->x = *x - border - text_width - padding * 2 + 1;
	button->y = (int)(ideal_height - text_height) / 2 - padding + 1;
	button->width = text_width + padding * 2;
	button->height = text_height + padding * 2;

	cairo_set_source_u32(cairo, haywardnag->type->border);
	cairo_rectangle(cairo, button->x - border, button->y - border,
			button->width + border * 2, button->height + border * 2);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, haywardnag->type->button_background);
	cairo_rectangle(cairo, button->x, button->y,
			button->width, button->height);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, haywardnag->type->button_text);
	cairo_move_to(cairo, button->x + padding, button->y + padding);
	render_text(cairo, haywardnag->type->font, 1, true,
			"%s", button->text);

	*x = button->x - border;

	return ideal_surface_height;
}

static uint32_t render_to_cairo(cairo_t *cairo, struct haywardnag *haywardnag) {
	uint32_t max_height = 0;

	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, haywardnag->type->background);
	cairo_paint(cairo);

	uint32_t h = render_message(cairo, haywardnag);
	max_height = h > max_height ? h : max_height;

	int x = haywardnag->width - haywardnag->type->button_margin_right;
	for (int i = 0; i < haywardnag->buttons->length; i++) {
		h = render_button(cairo, haywardnag, i, &x);
		max_height = h > max_height ? h : max_height;
		x -= haywardnag->type->button_gap;
		if (i == 0) {
			x -= haywardnag->type->button_gap_close;
		}
	}

	if (haywardnag->details.visible) {
		h = render_detailed(cairo, haywardnag, max_height);
		max_height = h > max_height ? h : max_height;
	}

	int border = haywardnag->type->bar_border_thickness;
	if (max_height > haywardnag->height) {
		max_height += border;
	}
	cairo_set_source_u32(cairo, haywardnag->type->border_bottom);
	cairo_rectangle(cairo, 0,
			haywardnag->height - border,
			haywardnag->width,
			border);
	cairo_fill(cairo);

	return max_height;
}

void render_frame(struct haywardnag *haywardnag) {
	if (!haywardnag->run_display) {
		return;
	}

	cairo_surface_t *recorder = cairo_recording_surface_create(
			CAIRO_CONTENT_COLOR_ALPHA, NULL);
	cairo_t *cairo = cairo_create(recorder);
	cairo_scale(cairo, haywardnag->scale, haywardnag->scale);
	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);
	cairo_restore(cairo);
	uint32_t height = render_to_cairo(cairo, haywardnag);
	if (height != haywardnag->height) {
		zwlr_layer_surface_v1_set_size(haywardnag->layer_surface, 0, height);
		zwlr_layer_surface_v1_set_exclusive_zone(haywardnag->layer_surface,
				height);
		wl_surface_commit(haywardnag->surface);
		wl_display_roundtrip(haywardnag->display);
	} else {
		haywardnag->current_buffer = get_next_buffer(haywardnag->shm,
				haywardnag->buffers,
				haywardnag->width * haywardnag->scale,
				haywardnag->height * haywardnag->scale);
		if (!haywardnag->current_buffer) {
			hayward_log(HAYWARD_DEBUG, "Failed to get buffer. Skipping frame.");
			goto cleanup;
		}

		cairo_t *shm = haywardnag->current_buffer->cairo;
		cairo_save(shm);
		cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
		cairo_paint(shm);
		cairo_restore(shm);
		cairo_set_source_surface(shm, recorder, 0.0, 0.0);
		cairo_paint(shm);

		wl_surface_set_buffer_scale(haywardnag->surface, haywardnag->scale);
		wl_surface_attach(haywardnag->surface,
				haywardnag->current_buffer->buffer, 0, 0);
		wl_surface_damage(haywardnag->surface, 0, 0,
				haywardnag->width, haywardnag->height);
		wl_surface_commit(haywardnag->surface);
		wl_display_roundtrip(haywardnag->display);
	}

cleanup:
	cairo_surface_destroy(recorder);
	cairo_destroy(cairo);
}
