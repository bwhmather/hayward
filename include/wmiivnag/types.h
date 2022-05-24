#ifndef _WMIIVNAG_TYPES_H
#define _WMIIVNAG_TYPES_H

struct wmiivnag_type {
	char *name;

	char *font;
	char *output;
	uint32_t anchors;
	int32_t layer; // enum zwlr_layer_shell_v1_layer or -1 if unset

	// Colors
	uint32_t button_text;
	uint32_t button_background;
	uint32_t details_background;
	uint32_t background;
	uint32_t text;
	uint32_t border;
	uint32_t border_bottom;

	// Sizing
	ssize_t bar_border_thickness;
	ssize_t message_padding;
	ssize_t details_border_thickness;
	ssize_t button_border_thickness;
	ssize_t button_gap;
	ssize_t button_gap_close;
	ssize_t button_margin_right;
	ssize_t button_padding;
};

struct wmiivnag_type *wmiivnag_type_new(const char *name);

void wmiivnag_types_add_default(list_t *types);

struct wmiivnag_type *wmiivnag_type_get(list_t *types, char *name);

struct wmiivnag_type *wmiivnag_type_clone(struct wmiivnag_type *type);

void wmiivnag_type_merge(struct wmiivnag_type *dest, struct wmiivnag_type *src);

void wmiivnag_type_free(struct wmiivnag_type *type);

void wmiivnag_types_free(list_t *types);

#endif
