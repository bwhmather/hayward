#ifndef _WMIIVBAR_INPUT_H
#define _WMIIVBAR_INPUT_H

#include <wayland-client.h>
#include <stdbool.h>
#include "list.h"

#define WMIIV_SCROLL_UP KEY_MAX + 1
#define WMIIV_SCROLL_DOWN KEY_MAX + 2
#define WMIIV_SCROLL_LEFT KEY_MAX + 3
#define WMIIV_SCROLL_RIGHT KEY_MAX + 4

#define WMIIV_CONTINUOUS_SCROLL_TIMEOUT 1000
#define WMIIV_CONTINUOUS_SCROLL_THRESHOLD 10000

struct wmiivbar;
struct wmiivbar_output;

struct wmiivbar_pointer {
	struct wl_pointer *pointer;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor_image *cursor_image;
	struct wl_surface *cursor_surface;
	struct wmiivbar_output *current;
	double x, y;
	uint32_t serial;
};

struct touch_slot {
	int32_t id;
	uint32_t time;
	struct wmiivbar_output *output;
	double start_x, start_y;
	double x, y;
};

struct wmiivbar_touch {
	struct wl_touch *touch;
	struct touch_slot slots[16];
};

enum hotspot_event_handling {
	HOTSPOT_IGNORE,
	HOTSPOT_PROCESS,
};

struct wmiivbar_hotspot {
	struct wl_list link; // wmiivbar_output::hotspots
	int x, y, width, height;
	enum hotspot_event_handling (*callback)(struct wmiivbar_output *output,
		struct wmiivbar_hotspot *hotspot, double x, double y, uint32_t button,
		void *data);
	void (*destroy)(void *data);
	void *data;
};

struct wmiivbar_scroll_axis {
	wl_fixed_t value;
	uint32_t discrete_steps;
	uint32_t update_time;
};

struct wmiivbar_seat {
	struct wmiivbar *bar;
	uint32_t wl_name;
	struct wl_seat *wl_seat;
	struct wmiivbar_pointer pointer;
	struct wmiivbar_touch touch;
	struct wl_list link; // wmiivbar_seat:link
	struct wmiivbar_scroll_axis axis[2];
};

extern const struct wl_seat_listener seat_listener;

void update_cursor(struct wmiivbar_seat *seat);

uint32_t event_to_x11_button(uint32_t event);

void free_hotspots(struct wl_list *list);

void wmiivbar_seat_free(struct wmiivbar_seat *seat);

#endif
