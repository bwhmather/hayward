#ifndef _HAYWARDBAR_INPUT_H
#define _HAYWARDBAR_INPUT_H

#include <stdbool.h>
#include <wayland-client.h>

#include "hayward-common/list.h"

#define HAYWARD_SCROLL_UP KEY_MAX + 1
#define HAYWARD_SCROLL_DOWN KEY_MAX + 2
#define HAYWARD_SCROLL_LEFT KEY_MAX + 3
#define HAYWARD_SCROLL_RIGHT KEY_MAX + 4

#define HAYWARD_CONTINUOUS_SCROLL_TIMEOUT 1000
#define HAYWARD_CONTINUOUS_SCROLL_THRESHOLD 10000

struct haywardbar;
struct haywardbar_output;

struct haywardbar_pointer {
    struct wl_pointer *pointer;
    struct wl_cursor_theme *cursor_theme;
    struct wl_cursor_image *cursor_image;
    struct wl_surface *cursor_surface;
    struct haywardbar_output *current;
    double x, y;
    uint32_t serial;
};

struct touch_slot {
    int32_t id;
    uint32_t time;
    struct haywardbar_output *output;
    double start_x, start_y;
    double x, y;
};

struct haywardbar_touch {
    struct wl_touch *touch;
    struct touch_slot slots[16];
};

enum hotspot_event_handling {
    HOTSPOT_IGNORE,
    HOTSPOT_PROCESS,
};

struct haywardbar_hotspot {
    struct wl_list link; // haywardbar_output::hotspots
    int x, y, width, height;
    enum hotspot_event_handling (*callback
    )(struct haywardbar_output *output, struct haywardbar_hotspot *hotspot,
      double x, double y, uint32_t button, void *data);
    void (*destroy)(void *data);
    void *data;
};

struct haywardbar_scroll_axis {
    wl_fixed_t value;
    uint32_t discrete_steps;
    uint32_t update_time;
};

struct haywardbar_seat {
    struct haywardbar *bar;
    uint32_t wl_name;
    struct wl_seat *wl_seat;
    struct haywardbar_pointer pointer;
    struct haywardbar_touch touch;
    struct wl_list link; // haywardbar_seat:link
    struct haywardbar_scroll_axis axis[2];
};

extern const struct wl_seat_listener seat_listener;

void
update_cursor(struct haywardbar_seat *seat);

uint32_t
event_to_x11_button(uint32_t event);

void
free_hotspots(struct wl_list *list);

void
haywardbar_seat_free(struct haywardbar_seat *seat);

#endif
