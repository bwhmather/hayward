#ifndef HWD_TREE_DRAG_ICON_H
#define HWD_TREE_DRAG_ICON_H

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_scene.h>

struct hwd_root;

struct hwd_drag_icon {
    struct hwd_root *root;
    struct wl_list link;

    struct wlr_drag_icon *wlr_drag_icon;

    struct wlr_scene_tree *scene_tree;
    struct {
        struct wlr_scene_tree *drag_icon;
    } layers;

    struct wl_listener destroy;
};

struct hwd_drag_icon *
drag_icon_create(struct hwd_root *root, struct wlr_drag_icon *drag_icon);

void
drag_icon_set_position(struct hwd_drag_icon *drag, int x, int y);

#endif
