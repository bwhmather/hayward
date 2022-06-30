#include "wmiiv/tree/column.h"
#include "wmiiv/tree/window.h"
#include "wmiiv/desktop.h"
#include "wmiiv/output.h"

void desktop_damage_surface(struct wlr_surface *surface, double lx, double ly,
		bool whole) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		wlr_output_layout_get_box(root->output_layout,
			output->wlr_output, &output_box);
		output_damage_surface(output, lx - output_box.x,
			ly - output_box.y, surface, whole);
	}
}

void desktop_damage_window(struct wmiiv_window *window) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_window(output, window);
	}
}


void desktop_damage_column(struct wmiiv_column *column) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_column(output, column);
	}
}

void desktop_damage_box(struct wlr_box *box) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_box(output, box);
	}
}

void desktop_damage_view(struct wmiiv_view *view) {
	desktop_damage_window(view->window);
	struct wlr_box box = {
		.x = view->window->current.content_x - view->geometry.x,
		.y = view->window->current.content_y - view->geometry.y,
		.width = view->surface->current.width,
		.height = view->surface->current.height,
	};
	desktop_damage_box(&box);
}
