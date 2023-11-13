#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "hayward/theme.h"

#include <cairo.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include <hayward-common/log.h>

#include <hayward/scene/cairo.h>
#include <hayward/scene/colours.h>

int
hwd_theme_window_get_titlebar_height(struct hwd_theme_window *theme) {
    return 26;
}

int
hwd_theme_window_get_shaded_titlebar_height(struct hwd_theme_window *theme) {
    return 26;
}

int
hwd_theme_window_get_border_left(struct hwd_theme_window *theme) {
    return theme->border.left_break - 1;
}

int
hwd_theme_window_get_border_right(struct hwd_theme_window *theme) {
    if (theme->border.buffer == NULL) {
        return 0;
    }
    return theme->border.buffer->width - theme->border.right_break - 1;
}

int
hwd_theme_window_get_border_top(struct hwd_theme_window *theme) {
    return theme->border.top_break - 1;
}

int
hwd_theme_window_get_border_bottom(struct hwd_theme_window *theme) {
    if (theme->border.buffer == NULL) {
        return 0;
    }
    return theme->border.buffer->height - theme->border.bottom_break - 1;
}

struct wlr_scene_node *
hwd_theme_create_column_separator_node(struct hwd_theme *theme, struct wlr_scene_tree *parent) {
    return NULL;
}

int
hwd_theme_get_column_separator_width(struct hwd_theme *theme) {
    return theme->column_separator.buffer->width;
}

void
hwd_theme_destroy(struct hwd_theme *theme) {}

static const float BORDER = 1.0;
static const float RADIUS = 8.0;
static const size_t SIZE = 32;

static const struct hwd_colour FOCUSED_COLOUR = {0.174, 0.404, 0.571, 1.0};
static const struct hwd_colour ACTIVE_COLOUR = {0.393, 0.393, 0.393, 1.0};
static const struct hwd_colour INACTIVE_COLOUR = {0.195, 0.195, 0.195, 1.0};
static const struct hwd_colour URGENT_COLOUR = {0.5, 0.1, 0.1, 1.0};
static const struct hwd_colour BORDER_OUTER_COLOUR = {0.11, 0.11, 0.11, 1.0};
static const struct hwd_colour BACKGROUND_COLOUR = {0.6, 0.6, 0.6, 1.0};

struct hwd_default_theme_colours {
    struct hwd_colour foreground;
    struct hwd_colour background_titlebar;
    struct hwd_colour background_content;
    struct hwd_colour border_outer;
    struct hwd_colour border_highlight;
    struct hwd_colour border_inner;
};

static const struct hwd_default_theme_colours COLOURS_FOCUSED = {
    .foreground = {1.0, 1.0, 1.0, 1.0},
    .background_titlebar = FOCUSED_COLOUR,
    .background_content = BACKGROUND_COLOUR,
    .border_outer = BORDER_OUTER_COLOUR,
    .border_highlight = FOCUSED_COLOUR,
    .border_inner = {0.15, 0.15, 0.15, 1.0},
};

static const struct hwd_default_theme_colours COLOURS_ACTIVE = {
    .foreground = {1.0, 1.0, 1.0, 1.0},
    .background_titlebar = ACTIVE_COLOUR,
    .background_content = BACKGROUND_COLOUR,
    .border_outer = BORDER_OUTER_COLOUR,
    .border_highlight = ACTIVE_COLOUR,
    .border_inner = {0.15, 0.15, 0.15, 1.0},
};

static const struct hwd_default_theme_colours COLOURS_INACTIVE = {
    .foreground = {1.0, 1.0, 1.0, 1.0},
    .background_titlebar = INACTIVE_COLOUR,
    .background_content = BACKGROUND_COLOUR,
    .border_outer = BORDER_OUTER_COLOUR,
    .border_highlight = INACTIVE_COLOUR,
    .border_inner = {0.15, 0.15, 0.15, 1.0},
};

static const struct hwd_default_theme_colours COLOURS_URGENT = {
    .foreground = {1.0, 1.0, 1.0, 1.0},
    .background_titlebar = URGENT_COLOUR,
    .background_content = BACKGROUND_COLOUR,
    .border_outer = BORDER_OUTER_COLOUR,
    .border_highlight = URGENT_COLOUR,
    .border_inner = {0.15, 0.15, 0.15, 1.0},
};

static void
fill_titlebar(cairo_t *cairo, struct hwd_default_theme_colours colours) {
    cairo_pattern_t *fill;
    struct hwd_colour c;

    cairo_save(cairo);

    fill = cairo_pattern_create_linear(0, 0, 0, SIZE);
    c = hwd_lighten(0.2, colours.background_titlebar);
    cairo_pattern_add_color_stop_rgba(fill, 0, c.r, c.g, c.b, c.a);
    c = colours.background_titlebar;
    cairo_pattern_add_color_stop_rgba(fill, 0.5 * RADIUS / SIZE, c.r, c.g, c.b, c.a);
    c = hwd_darken(0.1, colours.background_titlebar);
    cairo_pattern_add_color_stop_rgba(fill, 1, c.r, c.g, c.b, c.a);

    cairo_set_source(cairo, fill);
    cairo_fill(cairo);
    cairo_restore(cairo);
}

static void
stroke_border_outer(cairo_t *cairo, struct hwd_default_theme_colours colours) {
    struct hwd_colour c;

    cairo_save(cairo);
    cairo_set_line_width(cairo, BORDER);
    c = colours.border_outer;
    cairo_set_source_rgba(cairo, c.r, c.g, c.b, c.a);
    cairo_stroke(cairo);
    cairo_restore(cairo);
}

static void
fill_border_highlight(cairo_t *cairo, struct hwd_default_theme_colours colours) {
    struct hwd_colour c;

    cairo_save(cairo);
    c = colours.border_highlight;
    cairo_set_source_rgba(cairo, c.r, c.g, c.b, c.a);
    cairo_fill(cairo);
    cairo_restore(cairo);
}

static void
fill_background_content(cairo_t *cairo, struct hwd_default_theme_colours colours) {
    struct hwd_colour c;

    cairo_save(cairo);
    c = colours.background_content;
    cairo_set_source_rgba(cairo, c.r, c.g, c.b, c.a);
    cairo_fill(cairo);
    cairo_restore(cairo);
}

static void
stroke_border_inner(cairo_t *cairo, struct hwd_default_theme_colours colours) {
    struct hwd_colour c;

    cairo_save(cairo);
    cairo_set_line_width(cairo, BORDER);
    c = colours.border_inner;
    cairo_set_source_rgba(cairo, c.r, c.g, c.b, c.a);
    cairo_stroke(cairo);
    cairo_restore(cairo);
}

static void
outline_titlebar_floating(cairo_t *cairo) {
    cairo_move_to(cairo, 0.5 * BORDER, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, 0.5 * BORDER, RADIUS);
    cairo_arc(cairo, RADIUS, RADIUS, RADIUS - 0.5 * BORDER, -M_PI, -M_PI / 2);
    cairo_line_to(cairo, SIZE - RADIUS, 0.5 * BORDER);
    cairo_arc(cairo, SIZE - RADIUS, RADIUS, RADIUS - 0.5 * BORDER, -M_PI / 2, 0);
    cairo_line_to(cairo, SIZE - 0.5 * BORDER, SIZE - 0.5 * BORDER);
    cairo_close_path(cairo);
}

static void
outline_outer_border_floating(cairo_t *cairo) {
    cairo_move_to(cairo, 0.5 * BORDER, 0);
    cairo_line_to(cairo, 0.5 * BORDER, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE - 0.5 * BORDER, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE - 0.5 * BORDER, 0);
}

static void
outline_inner_border_floating(cairo_t *cairo) {
    cairo_move_to(cairo, 2.5 * BORDER, 0);
    cairo_line_to(cairo, 2.5 * BORDER, SIZE - 3.5 * BORDER);
    cairo_line_to(cairo, SIZE - 2.5 * BORDER, SIZE - 3.5 * BORDER);
    cairo_line_to(cairo, SIZE - 2.5 * BORDER, 0);
}

static struct hwd_theme_nineslice
gen_floating_titlebar(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    outline_titlebar_floating(cairo);
    fill_titlebar(cairo, colours);

    outline_titlebar_floating(cairo);
    stroke_border_outer(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 10, 22, 10, 22};
    return out;
}

static struct hwd_theme_nineslice
gen_floating_border(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    outline_outer_border_floating(cairo);
    fill_border_highlight(cairo, colours);

    outline_outer_border_floating(cairo);
    stroke_border_outer(cairo, colours);

    outline_inner_border_floating(cairo);
    fill_background_content(cairo, colours);

    outline_inner_border_floating(cairo);
    stroke_border_inner(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 4, 28, 1, 27};
    return out;
}

static struct hwd_theme_nineslice
gen_tiled_head_titlebar(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_move_to(cairo, 0, 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_close_path(cairo);
    fill_titlebar(cairo, colours);

    cairo_move_to(cairo, 0, 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, 0.5 * BORDER);
    cairo_move_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    stroke_border_outer(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 10, 22, 10, 22};
    return out;
}

static struct hwd_theme_nineslice
gen_tiled_head_shaded_titlebar(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_move_to(cairo, 0, 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_close_path(cairo);
    fill_titlebar(cairo, colours);

    cairo_move_to(cairo, 0, 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, 0.5 * BORDER);
    cairo_move_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    stroke_border_outer(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 10, 22, 10, 22};
    return out;
}
static struct hwd_theme_nineslice
gen_tiled_titlebar(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_move_to(cairo, 0, 0);
    cairo_line_to(cairo, SIZE, 0);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_close_path(cairo);
    fill_titlebar(cairo, colours);

    cairo_move_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    stroke_border_outer(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 10, 22, 10, 22};
    return out;
}

static struct hwd_theme_nineslice
gen_tiled_shaded_titlebar(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_move_to(cairo, 0, 0);
    cairo_line_to(cairo, SIZE, 0);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_close_path(cairo);
    fill_titlebar(cairo, colours);

    cairo_move_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    stroke_border_outer(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 10, 22, 10, 22};
    return out;
}

static struct hwd_theme_nineslice
gen_tiled_border(struct hwd_default_theme_colours colours) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(SIZE, SIZE);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_move_to(cairo, 0, 0);
    cairo_line_to(cairo, SIZE, 0);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_close_path(cairo);
    fill_border_highlight(cairo, colours);

    cairo_move_to(cairo, 0, SIZE - 0.5 * BORDER);
    cairo_line_to(cairo, SIZE, SIZE - 0.5 * BORDER);
    stroke_border_outer(cairo, colours);

    cairo_move_to(cairo, 2.5 * BORDER, 0);
    cairo_line_to(cairo, 2.5 * BORDER, SIZE - 2.5 * BORDER);
    cairo_line_to(cairo, SIZE - 2.5 * BORDER, SIZE - 2.5 * BORDER);
    cairo_line_to(cairo, SIZE - 2.5 * BORDER, 0);
    fill_background_content(cairo, colours);

    cairo_move_to(cairo, 2.5 * BORDER, 0);
    cairo_line_to(cairo, 2.5 * BORDER, SIZE - 2.5 * BORDER);
    cairo_line_to(cairo, SIZE - 2.5 * BORDER, SIZE - 2.5 * BORDER);
    cairo_line_to(cairo, SIZE - 2.5 * BORDER, 0);
    stroke_border_inner(cairo, colours);

    struct hwd_theme_nineslice out = {buffer, 4, 28, 1, 28};
    return out;
}

static struct hwd_theme_window
gen_single_floating(struct hwd_default_theme_colours colours) {
    struct hwd_theme_window window_theme = {
        .titlebar = gen_floating_titlebar(colours),
        .shaded_titlebar = {0},
        .border = gen_floating_border(colours),
        .text_font = NULL,
        .text_colour = colours.foreground,
    };
    return window_theme;
}

static struct hwd_theme_window
gen_single_tiled_head(struct hwd_default_theme_colours colours) {
    struct hwd_theme_window window_theme = {
        .titlebar = gen_tiled_head_titlebar(colours),
        .shaded_titlebar = gen_tiled_head_shaded_titlebar(colours),
        .border = gen_tiled_border(colours),
        .text_font = NULL,
        .text_colour = colours.foreground,
    };
    return window_theme;
}

static struct hwd_theme_window
gen_single_tiled(struct hwd_default_theme_colours colours) {
    struct hwd_theme_window window_theme = {
        .titlebar = gen_tiled_titlebar(colours),
        .shaded_titlebar = gen_tiled_shaded_titlebar(colours),
        .border = gen_tiled_border(colours),
        .text_font = NULL,
        .text_colour = colours.foreground,
    };
    return window_theme;
};

static struct hwd_theme_window_type
gen_floating(void) {
    struct hwd_theme_window_type theme = {
        .focused = gen_single_floating(COLOURS_FOCUSED),
        .active = gen_single_floating(COLOURS_ACTIVE),
        .inactive = gen_single_floating(COLOURS_INACTIVE),
        .urgent = gen_single_floating(COLOURS_URGENT),
    };
    return theme;
}

static struct hwd_theme_window_type
gen_tiled_head(void) {
    struct hwd_theme_window_type theme = {
        .focused = gen_single_tiled_head(COLOURS_FOCUSED),
        .active = gen_single_tiled_head(COLOURS_ACTIVE),
        .inactive = gen_single_tiled_head(COLOURS_INACTIVE),
        .urgent = gen_single_tiled_head(COLOURS_URGENT),
    };
    return theme;
}

static struct hwd_theme_window_type
gen_tiled(void) {
    struct hwd_theme_window_type theme = {
        .focused = gen_single_tiled(COLOURS_FOCUSED),
        .active = gen_single_tiled(COLOURS_ACTIVE),
        .inactive = gen_single_tiled(COLOURS_INACTIVE),
        .urgent = gen_single_tiled(COLOURS_URGENT),
    };
    return theme;
}

static struct hwd_theme_nineslice
gen_separator(void) {
    struct wlr_buffer *buffer = hwd_cairo_buffer_create(1, 8);
    cairo_t *cairo = hwd_cairo_buffer_get_context(buffer);

    cairo_move_to(cairo, 0.5, 0);
    cairo_line_to(cairo, 0.5, 8);
    cairo_set_line_width(cairo, 1.0);
    struct hwd_colour c = BORDER_OUTER_COLOUR;
    cairo_set_source_rgba(cairo, c.r, c.g, c.b, c.a);
    cairo_stroke(cairo);

    struct hwd_theme_nineslice out = {buffer, 0, 1, 0, 8};
    return out;
}

struct hwd_theme *
hwd_theme_create_default(void) {
    struct hwd_theme *theme = calloc(1, sizeof(struct hwd_theme));
    hwd_assert(theme != NULL, "Allocation failed");

    theme->floating = gen_floating();
    theme->tiled_head = gen_tiled_head();
    theme->tiled = gen_tiled();

    theme->column_separator = gen_separator();

    return theme;
}
