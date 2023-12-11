#ifndef HWD_SCENE_NINESLICE_H
#define HWD_SCENE_NINESLICE_H

#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

struct wlr_scene_node *
hwd_nineslice_node_create(
    struct wlr_scene_tree *parent,   //
    struct wlr_buffer *buffer,       //
    int left_break, int right_break, //
    int top_break, int bottom_break  //
);

void
hwd_nineslice_node_update(
    struct wlr_scene_node *node,     //
    struct wlr_buffer *buffer,       //
    int left_break, int right_break, //
    int top_break, int bottom_break  //
);

void
hwd_nineslice_node_set_size(struct wlr_scene_node *node, int width, int height);

#endif
