#ifndef HWD_SCENE_TEXT_H
#define HWD_SCENE_TEXT_H

#include <pango/pango.h>

#include <wlr/types/wlr_scene.h>

#include <hayward/scene/colours.h>

struct wlr_scene_node *
hwd_text_node_create(
    struct wlr_scene_tree *parent, char *text, struct hwd_colour color,
    PangoFontDescription *font_description
);

void
hwd_text_node_set_color(struct wlr_scene_node *node, struct hwd_colour color);

void
hwd_text_node_set_text(struct wlr_scene_node *node, char *text);

void
hwd_text_node_set_max_width(struct wlr_scene_node *node, int max_width);

#endif
