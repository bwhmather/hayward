#ifndef _SWAYNAG_SWAYNAG_H
#define _SWAYNAG_SWAYNAG_H
#include <stdint.h>
#include <strings.h>
#include "list.h"
#include "pool-buffer.h"
#include "wmiivnag/types.h"

#define SWAYNAG_MAX_HEIGHT 500

struct wmiivnag;

enum wmiivnag_action_type {
	SWAYNAG_ACTION_DISMISS,
	SWAYNAG_ACTION_EXPAND,
	SWAYNAG_ACTION_COMMAND,
};

struct wmiivnag_pointer {
	struct wl_pointer *pointer;
	uint32_t serial;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor_image *cursor_image;
	struct wl_surface *cursor_surface;
	int x;
	int y;
};

struct wmiivnag_seat {
	struct wl_seat *wl_seat;
	uint32_t wl_name;
	struct wmiivnag *wmiivnag;
	struct wmiivnag_pointer pointer;
	struct wl_list link;
};

struct wmiivnag_output {
	char *name;
	struct wl_output *wl_output;
	uint32_t wl_name;
	uint32_t scale;
	struct wmiivnag *wmiivnag;
	struct wl_list link;
};

struct wmiivnag_button {
	char *text;
	enum wmiivnag_action_type type;
	char *action;
	int x;
	int y;
	int width;
	int height;
	bool terminal;
	bool dismiss;
};

struct wmiivnag_details {
	bool visible;
	char *message;

	int x;
	int y;
	int width;
	int height;

	int offset;
	int visible_lines;
	int total_lines;
	struct wmiivnag_button button_details;
	struct wmiivnag_button button_up;
	struct wmiivnag_button button_down;
};

struct wmiivnag {
	bool run_display;

	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_seat *seat;
	struct wl_shm *shm;
	struct wl_list outputs;  // wmiivnag_output::link
	struct wl_list seats;  // wmiivnag_seat::link
	struct wmiivnag_output *output;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_surface *surface;

	uint32_t width;
	uint32_t height;
	int32_t scale;
	struct pool_buffer buffers[2];
	struct pool_buffer *current_buffer;

	struct wmiivnag_type *type;
	char *message;
	list_t *buttons;
	struct wmiivnag_details details;
};

void wmiivnag_setup(struct wmiivnag *wmiivnag);

void wmiivnag_run(struct wmiivnag *wmiivnag);

void wmiivnag_destroy(struct wmiivnag *wmiivnag);

#endif
