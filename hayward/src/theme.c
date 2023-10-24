#include "hayward/theme.h"

#include <pango/pango.h>

#include <hayward/scene/nineslice.h>

struct wlr_scene_node *
hwd_theme_nineslice_create_node(struct hwd_theme_nineslice *description) {
    return NULL;
}

struct wlr_scene_node *
hwd_theme_window_create_titlebar_node(
    struct hwd_theme_window *theme, struct wlr_scene_tree *parent
) {
    return NULL;
}

struct wlr_scene_node *
hwd_theme_window_create_border_node(struct hwd_theme_window *theme, struct wlr_scene_tree *parent) {
    return NULL;
}

int
hwd_theme_window_get_titlebar_height(struct hwd_theme_window *theme) {
    return 26;
}

int
hwd_theme_window_get_shaded_titlebar_height(struct hwd_theme_window *theme) {
    return 0;
}

int
hwd_theme_window_get_border_left(struct hwd_theme_window *theme) {
    return 0;
}

int
hwd_theme_window_get_border_right(struct hwd_theme_window *theme) {
    return 0;
}

int
hwd_theme_window_get_border_bottom(struct hwd_theme_window *theme) {
    return 0;
}

struct wlr_scene_node *
hwd_theme_create_column_separator_node(struct hwd_theme *theme, struct wlr_scene_tree *parent) {
    return NULL;
}

int
hwd_theme_get_column_separator_width(struct hwd_theme *theme) {
    return 0;
}

struct hwd_theme *
hwd_theme_create_default(void) {
    return NULL;
}

void
hwd_theme_destroy(struct hwd_theme *theme) {}
