#ifndef _HAYWARDNAG_HAYWARDNAG_H
#define _HAYWARDNAG_HAYWARDNAG_H
#include <stdint.h>
#include <strings.h>

#include "hayward-client/pool-buffer.h"
#include "hayward-common/list.h"

#include "haywardnag/types.h"

#define HAYWARDNAG_MAX_HEIGHT 500

struct haywardnag;

enum haywardnag_action_type {
    HAYWARDNAG_ACTION_DISMISS,
    HAYWARDNAG_ACTION_EXPAND,
    HAYWARDNAG_ACTION_COMMAND,
};

struct haywardnag_pointer {
    struct wl_pointer *pointer;
    uint32_t serial;
    struct wl_cursor_theme *cursor_theme;
    struct wl_cursor_image *cursor_image;
    struct wl_surface *cursor_surface;
    int x;
    int y;
};

struct haywardnag_seat {
    struct wl_seat *wl_seat;
    uint32_t wl_name;
    struct haywardnag *haywardnag;
    struct haywardnag_pointer pointer;
    struct wl_list link;
};

struct haywardnag_output {
    char *name;
    struct wl_output *wl_output;
    uint32_t wl_name;
    uint32_t scale;
    struct haywardnag *haywardnag;
    struct wl_list link;
};

struct haywardnag_button {
    char *text;
    enum haywardnag_action_type type;
    char *action;
    int x;
    int y;
    int width;
    int height;
    bool terminal;
    bool dismiss;
};

struct haywardnag_details {
    bool visible;
    char *message;

    int x;
    int y;
    int width;
    int height;

    int offset;
    int visible_lines;
    int total_lines;
    struct haywardnag_button button_details;
    struct haywardnag_button button_up;
    struct haywardnag_button button_down;
};

struct haywardnag {
    bool run_display;

    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_seat *seat;
    struct wl_shm *shm;
    struct wl_list outputs; // haywardnag_output::link
    struct wl_list seats;   // haywardnag_seat::link
    struct haywardnag_output *output;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_surface *surface;

    uint32_t width;
    uint32_t height;
    int32_t scale;
    struct pool_buffer buffers[2];
    struct pool_buffer *current_buffer;

    struct haywardnag_type *type;
    char *message;
    list_t *buttons;
    struct haywardnag_details details;
};

void
haywardnag_setup(struct haywardnag *haywardnag);

void
haywardnag_run(struct haywardnag *haywardnag);

void
haywardnag_destroy(struct haywardnag *haywardnag);

#endif
