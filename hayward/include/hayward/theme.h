#ifndef HWD_THEME_H
#define HWD_THEME_H

#include <pango/pango.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

#include <hayward/scene/colours.h>

struct hwd_theme_nineslice {
    struct wlr_buffer *buffer;
    int left_break;
    int right_break;
    int top_break;
    int bottom_break;
};

struct wlr_scene_node *
hwd_theme_nineslice_create_node(struct hwd_theme_nineslice *description);

struct hwd_theme_window {
    struct hwd_theme_nineslice titlebar;
    struct hwd_theme_nineslice shaded_titlebar;
    struct hwd_theme_nineslice border;

    PangoFontDescription *text_font;
    struct hwd_colour text_colour;
};

int
hwd_theme_window_get_titlebar_height(struct hwd_theme_window *theme);

int
hwd_theme_window_get_shaded_titlebar_height(struct hwd_theme_window *theme);

int
hwd_theme_window_get_border_left(struct hwd_theme_window *theme);

int
hwd_theme_window_get_border_right(struct hwd_theme_window *theme);

int
hwd_theme_window_get_border_top(struct hwd_theme_window *theme);

int
hwd_theme_window_get_border_bottom(struct hwd_theme_window *theme);

struct hwd_theme_window_type {
    struct hwd_theme_window focused;
    struct hwd_theme_window active;
    struct hwd_theme_window inactive;
    struct hwd_theme_window urgent;
};

struct hwd_theme {
    struct hwd_theme_window_type floating;
    struct hwd_theme_window_type tiled_head;
    struct hwd_theme_window_type tiled;

    struct hwd_theme_nineslice column_preview;
    struct hwd_theme_nineslice column_separator;
};

struct wlr_scene_node *
hwd_theme_create_column_separator_node(struct hwd_theme *theme, struct wlr_scene_tree *parent);

int
hwd_theme_get_column_separator_width(struct hwd_theme *theme);

void
hwd_theme_destroy(struct hwd_theme *theme);

struct hwd_theme *
hwd_theme_create_default(void);

#endif
