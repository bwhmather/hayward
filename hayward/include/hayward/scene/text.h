#ifndef _HAYWARD_SCENE_TEXT_H
#define _HAYWARD_SCENE_TEXT_H
#include <wlr/types/wlr_scene.h>

struct hayward_text_node {
    int width;
    int max_width;
    int height;
    int baseline;
    bool pango_markup;
    float color[4];

    struct wlr_scene_node *node;
};

struct hayward_text_node *
hayward_text_node_create(
    struct wlr_scene_tree *parent, char *text, const float *color,
    bool pango_markup
);

void
hayward_text_node_set_color(struct hayward_text_node *node, const float *color);

void
hayward_text_node_set_text(struct hayward_text_node *node, char *text);

void
hayward_text_node_set_max_width(struct hayward_text_node *node, int max_width);

#endif
