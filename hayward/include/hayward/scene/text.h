#ifndef HWD_SCENE_TEXT_H
#define HWD_SCENE_TEXT_H

#include <stdbool.h>
#include <wlr/types/wlr_scene.h>

struct hwd_text_node {
    int width;
    int max_width;
    int height;
    int baseline;
    bool pango_markup;
    float color[4];

    struct wlr_scene_node *node;
};

struct hwd_text_node *
hwd_text_node_create(
    struct wlr_scene_tree *parent, char *text, const float *color, bool pango_markup
);

void
hwd_text_node_set_color(struct hwd_text_node *node, const float *color);

void
hwd_text_node_set_text(struct hwd_text_node *node, char *text);

void
hwd_text_node_set_max_width(struct hwd_text_node *node, int max_width);

#endif
