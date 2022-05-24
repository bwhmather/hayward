#include <stdint.h>
#include "cairo_util.h"
#include "log.h"
#include "pango.h"
#include "pool-buffer.h"
#include "wmiivnag/wmiivnag.h"
#include "wmiivnag/types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static uint32_t render_message(cairo_t *cairo, struct wmiivnag *wmiivnag) {
	int text_width, text_height;
	get_text_size(cairo, wmiivnag->type->font, &text_width, &text_height, NULL,
			1, true, "%s", wmiivnag->message);

	int padding = wmiivnag->type->message_padding;

	uint32_t ideal_height = text_height + padding * 2;
	uint32_t ideal_surface_height = ideal_height;
	if (wmiivnag->height < ideal_surface_height) {
		return ideal_surface_height;
	}

	cairo_set_source_u32(cairo, wmiivnag->type->text);
	cairo_move_to(cairo, padding, (int)(ideal_height - text_height) / 2);
	render_text(cairo, wmiivnag->type->font, 1, false,
			"%s", wmiivnag->message);

	return ideal_surface_height;
}

static void render_details_scroll_button(cairo_t *cairo,
		struct wmiivnag *wmiivnag, struct wmiivnag_button *button) {
	int text_width, text_height;
	get_text_size(cairo, wmiivnag->type->font, &text_width, &text_height, NULL,
			1, true, "%s", button->text);

	int border = wmiivnag->type->button_border_thickness;
	int padding = wmiivnag->type->button_padding;

	cairo_set_source_u32(cairo, wmiivnag->type->details_background);
	cairo_rectangle(cairo, button->x, button->y,
			button->width, button->height);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, wmiivnag->type->button_background);
	cairo_rectangle(cairo, button->x + border, button->y + border,
			button->width - (border * 2), button->height - (border * 2));
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, wmiivnag->type->button_text);
	cairo_move_to(cairo, button->x + border + padding,
			button->y + border + (button->height - text_height) / 2);
	render_text(cairo, wmiivnag->type->font, 1, true,
			"%s", button->text);
}

static int get_detailed_scroll_button_width(cairo_t *cairo,
		struct wmiivnag *wmiivnag) {
	int up_width, down_width, temp_height;
	get_text_size(cairo, wmiivnag->type->font, &up_width, &temp_height, NULL,
			1, true,
			"%s", wmiivnag->details.button_up.text);
	get_text_size(cairo, wmiivnag->type->font, &down_width, &temp_height, NULL,
			1, true,
			"%s", wmiivnag->details.button_down.text);

	int text_width =  up_width > down_width ? up_width : down_width;
	int border = wmiivnag->type->button_border_thickness;
	int padding = wmiivnag->type->button_padding;

	return text_width + border * 2 + padding * 2;
}

static uint32_t render_detailed(cairo_t *cairo, struct wmiivnag *wmiivnag,
		uint32_t y) {
	uint32_t width = wmiivnag->width;

	int border = wmiivnag->type->details_border_thickness;
	int padding = wmiivnag->type->message_padding;
	int decor = padding + border;

	wmiivnag->details.x = decor;
	wmiivnag->details.y = y + decor;
	wmiivnag->details.width = width - decor * 2;

	PangoLayout *layout = get_pango_layout(cairo, wmiivnag->type->font,
			wmiivnag->details.message, 1, false);
	pango_layout_set_width(layout,
			(wmiivnag->details.width - padding * 2) * PANGO_SCALE);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	pango_layout_set_single_paragraph_mode(layout, false);
	pango_cairo_update_layout(cairo, layout);
	wmiivnag->details.total_lines = pango_layout_get_line_count(layout);

	PangoLayoutLine *line;
	line = pango_layout_get_line_readonly(layout, wmiivnag->details.offset);
	gint offset = line->start_index;
	const char *text = pango_layout_get_text(layout);
	pango_layout_set_text(layout, text + offset, strlen(text) - offset);

	int text_width, text_height;
	pango_cairo_update_layout(cairo, layout);
	pango_layout_get_pixel_size(layout, &text_width, &text_height);

	bool show_buttons = wmiivnag->details.offset > 0;
	int button_width = get_detailed_scroll_button_width(cairo, wmiivnag);
	if (show_buttons) {
		wmiivnag->details.width -= button_width;
		pango_layout_set_width(layout,
				(wmiivnag->details.width - padding * 2) * PANGO_SCALE);
	}

	uint32_t ideal_height;
	do {
		ideal_height = wmiivnag->details.y + text_height + decor + padding * 2;
		if (ideal_height > SWAYNAG_MAX_HEIGHT) {
			ideal_height = SWAYNAG_MAX_HEIGHT;

			if (!show_buttons) {
				show_buttons = true;
				wmiivnag->details.width -= button_width;
				pango_layout_set_width(layout,
						(wmiivnag->details.width - padding * 2) * PANGO_SCALE);
			}
		}

		wmiivnag->details.height = ideal_height - wmiivnag->details.y - decor;
		pango_layout_set_height(layout,
				(wmiivnag->details.height - padding * 2) * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_cairo_update_layout(cairo, layout);
		pango_layout_get_pixel_size(layout, &text_width, &text_height);
	} while (text_height != (wmiivnag->details.height - padding * 2));

	wmiivnag->details.visible_lines = pango_layout_get_line_count(layout);

	if (show_buttons) {
		wmiivnag->details.button_up.x =
			wmiivnag->details.x + wmiivnag->details.width;
		wmiivnag->details.button_up.y = wmiivnag->details.y;
		wmiivnag->details.button_up.width = button_width;
		wmiivnag->details.button_up.height = wmiivnag->details.height / 2;
		render_details_scroll_button(cairo, wmiivnag,
				&wmiivnag->details.button_up);

		wmiivnag->details.button_down.x =
			wmiivnag->details.x + wmiivnag->details.width;
		wmiivnag->details.button_down.y =
			wmiivnag->details.button_up.y + wmiivnag->details.button_up.height;
		wmiivnag->details.button_down.width = button_width;
		wmiivnag->details.button_down.height = wmiivnag->details.height / 2;
		render_details_scroll_button(cairo, wmiivnag,
				&wmiivnag->details.button_down);
	}

	cairo_set_source_u32(cairo, wmiivnag->type->details_background);
	cairo_rectangle(cairo, wmiivnag->details.x, wmiivnag->details.y,
			wmiivnag->details.width, wmiivnag->details.height);
	cairo_fill(cairo);

	cairo_move_to(cairo, wmiivnag->details.x + padding,
			wmiivnag->details.y + padding);
	cairo_set_source_u32(cairo, wmiivnag->type->text);
	pango_cairo_show_layout(cairo, layout);
	g_object_unref(layout);

	return ideal_height;
}

static uint32_t render_button(cairo_t *cairo, struct wmiivnag *wmiivnag,
		int button_index, int *x) {
	struct wmiivnag_button *button = wmiivnag->buttons->items[button_index];

	int text_width, text_height;
	get_text_size(cairo, wmiivnag->type->font, &text_width, &text_height, NULL,
			1, true, "%s", button->text);

	int border = wmiivnag->type->button_border_thickness;
	int padding = wmiivnag->type->button_padding;

	uint32_t ideal_height = text_height + padding * 2 + border * 2;
	uint32_t ideal_surface_height = ideal_height;
	if (wmiivnag->height < ideal_surface_height) {
		return ideal_surface_height;
	}

	button->x = *x - border - text_width - padding * 2 + 1;
	button->y = (int)(ideal_height - text_height) / 2 - padding + 1;
	button->width = text_width + padding * 2;
	button->height = text_height + padding * 2;

	cairo_set_source_u32(cairo, wmiivnag->type->border);
	cairo_rectangle(cairo, button->x - border, button->y - border,
			button->width + border * 2, button->height + border * 2);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, wmiivnag->type->button_background);
	cairo_rectangle(cairo, button->x, button->y,
			button->width, button->height);
	cairo_fill(cairo);

	cairo_set_source_u32(cairo, wmiivnag->type->button_text);
	cairo_move_to(cairo, button->x + padding, button->y + padding);
	render_text(cairo, wmiivnag->type->font, 1, true,
			"%s", button->text);

	*x = button->x - border;

	return ideal_surface_height;
}

static uint32_t render_to_cairo(cairo_t *cairo, struct wmiivnag *wmiivnag) {
	uint32_t max_height = 0;

	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_u32(cairo, wmiivnag->type->background);
	cairo_paint(cairo);

	uint32_t h = render_message(cairo, wmiivnag);
	max_height = h > max_height ? h : max_height;

	int x = wmiivnag->width - wmiivnag->type->button_margin_right;
	for (int i = 0; i < wmiivnag->buttons->length; i++) {
		h = render_button(cairo, wmiivnag, i, &x);
		max_height = h > max_height ? h : max_height;
		x -= wmiivnag->type->button_gap;
		if (i == 0) {
			x -= wmiivnag->type->button_gap_close;
		}
	}

	if (wmiivnag->details.visible) {
		h = render_detailed(cairo, wmiivnag, max_height);
		max_height = h > max_height ? h : max_height;
	}

	int border = wmiivnag->type->bar_border_thickness;
	if (max_height > wmiivnag->height) {
		max_height += border;
	}
	cairo_set_source_u32(cairo, wmiivnag->type->border_bottom);
	cairo_rectangle(cairo, 0,
			wmiivnag->height - border,
			wmiivnag->width,
			border);
	cairo_fill(cairo);

	return max_height;
}

void render_frame(struct wmiivnag *wmiivnag) {
	if (!wmiivnag->run_display) {
		return;
	}

	cairo_surface_t *recorder = cairo_recording_surface_create(
			CAIRO_CONTENT_COLOR_ALPHA, NULL);
	cairo_t *cairo = cairo_create(recorder);
	cairo_scale(cairo, wmiivnag->scale, wmiivnag->scale);
	cairo_save(cairo);
	cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cairo);
	cairo_restore(cairo);
	uint32_t height = render_to_cairo(cairo, wmiivnag);
	if (height != wmiivnag->height) {
		zwlr_layer_surface_v1_set_size(wmiivnag->layer_surface, 0, height);
		zwlr_layer_surface_v1_set_exclusive_zone(wmiivnag->layer_surface,
				height);
		wl_surface_commit(wmiivnag->surface);
		wl_display_roundtrip(wmiivnag->display);
	} else {
		wmiivnag->current_buffer = get_next_buffer(wmiivnag->shm,
				wmiivnag->buffers,
				wmiivnag->width * wmiivnag->scale,
				wmiivnag->height * wmiivnag->scale);
		if (!wmiivnag->current_buffer) {
			wmiiv_log(SWAY_DEBUG, "Failed to get buffer. Skipping frame.");
			goto cleanup;
		}

		cairo_t *shm = wmiivnag->current_buffer->cairo;
		cairo_save(shm);
		cairo_set_operator(shm, CAIRO_OPERATOR_CLEAR);
		cairo_paint(shm);
		cairo_restore(shm);
		cairo_set_source_surface(shm, recorder, 0.0, 0.0);
		cairo_paint(shm);

		wl_surface_set_buffer_scale(wmiivnag->surface, wmiivnag->scale);
		wl_surface_attach(wmiivnag->surface,
				wmiivnag->current_buffer->buffer, 0, 0);
		wl_surface_damage(wmiivnag->surface, 0, 0,
				wmiivnag->width, wmiivnag->height);
		wl_surface_commit(wmiivnag->surface);
		wl_display_roundtrip(wmiivnag->display);
	}

cleanup:
	cairo_surface_destroy(recorder);
	cairo_destroy(cairo);
}
