#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "hayward/scene/nineslice.h"

#include <wayland-util.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward-common/log.h>

struct hwd_nineslice_slices {
    struct wlr_scene_buffer *tl;
    struct wlr_scene_buffer *tc;
    struct wlr_scene_buffer *tr;
    struct wlr_scene_buffer *cl;
    struct wlr_scene_buffer *cc;
    struct wlr_scene_buffer *cr;
    struct wlr_scene_buffer *bl;
    struct wlr_scene_buffer *bc;
    struct wlr_scene_buffer *br;
};

static void
hwd_nineslice_unpack(struct wlr_scene_node *node, struct hwd_nineslice_slices *out) {
    struct wlr_scene_tree *root = wlr_scene_tree_from_node(node);
    struct wlr_scene_node *next;

    next = wl_container_of(root->children.next, next, link);
    out->tl = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->tl->node.link.next, next, link);
    out->tc = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->tc->node.link.next, next, link);
    out->tr = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->tr->node.link.next, next, link);
    out->cl = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->cl->node.link.next, next, link);
    out->cc = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->cc->node.link.next, next, link);
    out->cr = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->cr->node.link.next, next, link);
    out->bl = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->bl->node.link.next, next, link);
    out->bc = wlr_scene_buffer_from_node(next);
    next = wl_container_of(out->bc->node.link.next, next, link);
    out->br = wlr_scene_buffer_from_node(next);
}

struct wlr_scene_node *
hwd_nineslice_node_create(
    struct wlr_scene_tree *parent,   //
    struct wlr_buffer *buffer,       //
    int left_break, int right_break, //
    int top_break, int bottom_break  //
) {
    struct wlr_scene_tree *root = wlr_scene_tree_create(parent);
    hwd_assert(root != NULL, "Unable to allocate nineslice tree root");

    struct wlr_scene_buffer *slice;

    // Top-left.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Top-centre.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Top-right.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Centre-left.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Centre-centre.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Centre-right.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Bottom-left.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Bottom-centre.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    // Bottom-right.
    slice = wlr_scene_buffer_create(root, buffer);
    hwd_assert(slice != NULL, "Unable to allocate slice");

    hwd_nineslice_node_update(
        &root->node, buffer, left_break, right_break, top_break, bottom_break
    );

    int buffer_width = 0;
    int buffer_height = 0;
    if (buffer != NULL) {
        buffer_width = buffer->width;
        buffer_height = buffer->height;
    }
    hwd_nineslice_node_set_size(&root->node, buffer_width, buffer_height);

    return &root->node;
}

void
hwd_nineslice_node_update(
    struct wlr_scene_node *node,     //
    struct wlr_buffer *buffer,       //
    int left_break, int right_break, //
    int top_break, int bottom_break  //
) {
    hwd_assert(node != NULL, "Expected node");

    int buffer_width = 0;
    int buffer_height = 0;
    if (buffer != NULL) {
        buffer_width = buffer->width;
        buffer_height = buffer->height;
    }

    int left_width = left_break;
    int centre_width = right_break - left_break;
    int right_width = buffer_width - right_break;

    int top_height = top_break;
    int centre_height = bottom_break - top_break;
    int bottom_height = buffer_height - bottom_break;

    struct hwd_nineslice_slices slices;
    hwd_nineslice_unpack(node, &slices);

    struct wlr_fbox src_box = {0};

    // Top-left.
    src_box.x = 0;
    src_box.y = 0;
    src_box.width = left_width;
    src_box.height = top_height;
    wlr_scene_buffer_set_buffer(slices.tl, buffer);
    wlr_scene_buffer_set_source_box(slices.tl, &src_box);

    // Top-centre.
    src_box.x = left_break;
    src_box.y = 0;
    src_box.width = centre_width;
    src_box.height = top_height;
    wlr_scene_buffer_set_buffer(slices.tc, buffer);
    wlr_scene_buffer_set_source_box(slices.tc, &src_box);

    // Top-right.
    src_box.x = right_break;
    src_box.y = 0;
    src_box.width = right_width;
    src_box.height = top_height;
    wlr_scene_buffer_set_buffer(slices.tr, buffer);
    wlr_scene_buffer_set_source_box(slices.tr, &src_box);

    // Centre-left.
    src_box.x = 0;
    src_box.y = top_break;
    src_box.width = left_width;
    src_box.height = centre_height;
    wlr_scene_buffer_set_buffer(slices.cl, buffer);
    wlr_scene_buffer_set_source_box(slices.cl, &src_box);

    // Centre-centre.
    src_box.x = left_break;
    src_box.y = top_break;
    src_box.width = centre_width;
    src_box.height = centre_height;
    wlr_scene_buffer_set_buffer(slices.cc, buffer);
    wlr_scene_buffer_set_source_box(slices.cc, &src_box);

    // Centre-right.
    src_box.x = right_break;
    src_box.y = top_break;
    src_box.width = right_width;
    src_box.height = centre_height;
    wlr_scene_buffer_set_buffer(slices.cr, buffer);
    wlr_scene_buffer_set_source_box(slices.cr, &src_box);

    // Bottom-left.
    src_box.x = 0;
    src_box.y = bottom_break;
    src_box.width = left_width;
    src_box.height = bottom_height;
    wlr_scene_buffer_set_buffer(slices.bl, buffer);
    wlr_scene_buffer_set_source_box(slices.bl, &src_box);

    // Bottom-centre.
    src_box.x = left_break;
    src_box.y = bottom_break;
    src_box.width = centre_width;
    src_box.height = bottom_height;
    wlr_scene_buffer_set_buffer(slices.bc, buffer);
    wlr_scene_buffer_set_source_box(slices.bc, &src_box);

    // Bottom-right.
    src_box.x = right_break;
    src_box.y = bottom_break;
    src_box.width = right_width;
    src_box.height = bottom_height;
    wlr_scene_buffer_set_buffer(slices.br, buffer);
    wlr_scene_buffer_set_source_box(slices.br, &src_box);
}

void
hwd_nineslice_node_set_size(struct wlr_scene_node *node, int width, int height) {
    hwd_assert(node != NULL, "Expected node");

    struct hwd_nineslice_slices slices;
    hwd_nineslice_unpack(node, &slices);

    if (slices.tl->buffer == NULL) {
        return;
    }

    int left_width = slices.tl->src_box.width;
    int right_width = slices.tr->src_box.width;
    int centre_width = width - left_width - right_width;
    if (centre_width < 0) {
        centre_width = 0;
    }

    int top_height = slices.tl->src_box.height;
    int bottom_height = slices.bl->src_box.height;
    int centre_height = height - top_height - bottom_height;
    if (centre_height < 0) {
        centre_height = 0;
    }

    int left_break = left_width;
    int right_break = left_width + centre_width;
    int top_break = top_height;
    int bottom_break = top_height + centre_height;

    // Top-left.
    wlr_scene_node_set_position(&slices.tl->node, 0, 0);
    wlr_scene_buffer_set_dest_size(slices.tl, left_width, top_height);

    // Top-centre.
    wlr_scene_node_set_position(&slices.tc->node, left_break, 0);
    wlr_scene_buffer_set_dest_size(slices.tc, centre_width, top_height);

    // Top-right.
    wlr_scene_node_set_position(&slices.tr->node, right_break, 0);
    wlr_scene_buffer_set_dest_size(slices.tr, right_width, top_height);

    // Centre-left.
    wlr_scene_node_set_position(&slices.cl->node, 0, top_break);
    wlr_scene_buffer_set_dest_size(slices.cl, left_width, centre_height);

    // Centre-centre.
    wlr_scene_node_set_position(&slices.cc->node, left_break, top_break);
    wlr_scene_buffer_set_dest_size(slices.cc, centre_width, centre_height);

    // Centre-right.
    wlr_scene_node_set_position(&slices.cr->node, right_break, top_break);
    wlr_scene_buffer_set_dest_size(slices.cr, right_width, centre_height);

    // Bottom-left.
    wlr_scene_node_set_position(&slices.bl->node, 0, bottom_break);
    wlr_scene_buffer_set_dest_size(slices.bl, left_width, bottom_height);

    // Bottom-centre.
    wlr_scene_node_set_position(&slices.bc->node, left_break, bottom_break);
    wlr_scene_buffer_set_dest_size(slices.bc, centre_width, bottom_height);

    // Bottom-right.
    wlr_scene_node_set_position(&slices.br->node, right_break, bottom_break);
    wlr_scene_buffer_set_dest_size(slices.br, right_width, bottom_height);
}
