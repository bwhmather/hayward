#define _POSIX_C_SOURCE 200809L
#include "hayward/scene/nineslice.h"

#include <hayward-common/log.h>

struct hwd_nineslice {};

struct wlr_scene_node *
hwd_nineslice_node_create(
    struct wlr_scene_tree *parent,   //
    struct wlr_buffer *buffer,       //
    int left_break, int right_break, //
    int top_break, int bottom_break  //
) {
    int buffer_width = buffer->width;
    int buffer_height = buffer->height;

    struct wlr_scene_tree *root = wlr_scene_tree_create(parent);
    hwd_assert(root != NULL, "Unable to allocate nineslice tree root");

    struct wlr_scene_buffer *slice;
    struct wlr_fbox src_box = {0};

    // Top-left.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = 0;
    src_box.y = 0;
    src_box.width = left_break;
    src_box.height = top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Top-centre.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = left_break;
    src_box.y = 0;
    src_box.width = right_break - left_break;
    src_box.height = top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Top-right.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = right_break;
    src_box.y = 0;
    src_box.width = buffer_width - right_break;
    src_box.height = top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Centre-left.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = 0;
    src_box.y = top_break;
    src_box.width = left_break;
    src_box.height = bottom_break - top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Centre-centre.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = left_break;
    src_box.y = top_break;
    src_box.width = right_break - left_break;
    src_box.height = bottom_break - top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Centre-right.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = right_break;
    src_box.y = top_break;
    src_box.width = buffer_width - right_break;
    src_box.height = bottom_break - top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Bottom-left.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = 0;
    src_box.y = bottom_break;
    src_box.width = left_break;
    src_box.height = buffer_height - top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Bottom-centre.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = left_break;
    src_box.y = bottom_break;
    src_box.width = right_break - left_break;
    src_box.height = buffer_height - top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    // Bottom-right.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    src_box.x = right_break;
    src_box.y = bottom_break;
    src_box.width = buffer_width - right_break;
    src_box.height = buffer_height - top_break;
    wlr_scene_buffer_set_source_box(slice, &src_box);

    hwd_nineslice_node_set_size(&root->node, buffer_width, buffer_height);

    return &root->node;
}

void
hwd_nineslice_node_set_size(struct wlr_scene_node *node, int width, int height) {
    struct wlr_scene_tree *root = wlr_scene_tree_from_node(node);
    struct wlr_scene_node *next;

    next = wl_container_of(root->children.next, next, link);
    struct wlr_scene_buffer *tl = wlr_scene_buffer_from_node(next);
    next = wl_container_of(tl->node.link.next, next, link);
    struct wlr_scene_buffer *tc = wlr_scene_buffer_from_node(next);
    next = wl_container_of(tc->node.link.next, next, link);
    struct wlr_scene_buffer *tr = wlr_scene_buffer_from_node(next);
    next = wl_container_of(tr->node.link.next, next, link);
    struct wlr_scene_buffer *cl = wlr_scene_buffer_from_node(next);
    next = wl_container_of(cl->node.link.next, next, link);
    struct wlr_scene_buffer *cc = wlr_scene_buffer_from_node(next);
    next = wl_container_of(cc->node.link.next, next, link);
    struct wlr_scene_buffer *cr = wlr_scene_buffer_from_node(next);
    next = wl_container_of(cr->node.link.next, next, link);
    struct wlr_scene_buffer *bl = wlr_scene_buffer_from_node(next);
    next = wl_container_of(bl->node.link.next, next, link);
    struct wlr_scene_buffer *bc = wlr_scene_buffer_from_node(next);
    next = wl_container_of(bc->node.link.next, next, link);
    struct wlr_scene_buffer *br = wlr_scene_buffer_from_node(next);

    int buffer_width = tl->buffer->width;
    int buffer_height = tl->buffer->height;
    int left_break = tl->src_box.width;
    int right_break = tc->src_box.width + left_break;
    int top_break = tl->src_box.height;
    int bottom_break = cl->src_box.height + top_break;

    if (width < (left_break + buffer_width - right_break)) {
        width = left_break + buffer_width - right_break;
    }
    if (height < (top_break + buffer_height - bottom_break)) {
        height = top_break + buffer_height - bottom_break;
    }

    // Top-left.
    wlr_scene_node_set_position(&tl->node, 0, 0);
    wlr_scene_buffer_set_dest_size(tl, left_break, top_break);

    // Top-centre.
    wlr_scene_node_set_position(&tc->node, left_break, 0);
    wlr_scene_buffer_set_dest_size(
        tc, width - left_break - (buffer_width - right_break), top_break
    );

    // Top-right.
    wlr_scene_node_set_position(&tr->node, right_break, 0);
    wlr_scene_buffer_set_dest_size(tr, buffer_width - right_break, top_break);

    // Centre-left.
    wlr_scene_node_set_position(&cl->node, 0, top_break);
    wlr_scene_buffer_set_dest_size(
        cl, left_break, height - top_break - (buffer_height - bottom_break)
    );

    // Centre-centre.
    wlr_scene_node_set_position(&cc->node, left_break, top_break);
    wlr_scene_buffer_set_dest_size(
        cc, width - left_break - (buffer_width - right_break),
        height - top_break - (buffer_height - bottom_break)
    );

    // Centre-right.
    wlr_scene_node_set_position(&cr->node, right_break, top_break);
    wlr_scene_buffer_set_dest_size(
        cr, buffer_width - right_break, height - top_break - (buffer_height - bottom_break)
    );

    // Bottom-left.
    wlr_scene_node_set_position(&bl->node, 0, bottom_break);
    wlr_scene_buffer_set_dest_size(bl, left_break, buffer_height - bottom_break);

    // Bottom-centre.
    wlr_scene_node_set_position(&bc->node, left_break, bottom_break);
    wlr_scene_buffer_set_dest_size(
        bc, width - left_break - (buffer_width - right_break), buffer_height - bottom_break
    );

    // Bottom-right.
    wlr_scene_node_set_position(&br->node, right_break, bottom_break);
    wlr_scene_buffer_set_dest_size(br, buffer_width - right_break, buffer_height - bottom_break);
}
