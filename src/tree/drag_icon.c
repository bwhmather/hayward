#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/drag_icon.h"

#include <assert.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <hayward/tree/root.h>

static void
drag_init_scene(struct hwd_drag_icon *drag) {
    drag->scene_tree = wlr_scene_tree_create(NULL);
    assert(drag->scene_tree != NULL);

    drag->layers.drag_icon = wlr_scene_drag_icon_create(drag->scene_tree, drag->wlr_drag_icon);
    assert(drag->layers.drag_icon != NULL);
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_drag_icon *drag = wl_container_of(listener, drag, destroy);

    wl_list_remove(&drag->link);
    wl_list_remove(&drag->destroy.link);

    free(drag);
}

struct hwd_drag_icon *
drag_icon_create(struct hwd_root *root, struct wlr_drag_icon *drag_icon) {
    assert(root != NULL);

    struct hwd_drag_icon *drag = calloc(1, sizeof(struct hwd_drag_icon));
    if (!drag) {
        wlr_log(WLR_ERROR, "Unable to allocate hwd_drag");
        return NULL;
    }

    drag->root = root;
    wl_list_insert(&root->drag_icons, &drag->link);

    drag->wlr_drag_icon = drag_icon;

    drag_init_scene(drag);

    wl_signal_add(&drag_icon->events.destroy, &drag->destroy);
    drag->destroy.notify = handle_destroy;

    root_set_dirty(root);

    return drag;
}

void
drag_icon_set_position(struct hwd_drag_icon *drag, int x, int y) {
    assert(drag != NULL);
    assert(drag->wlr_drag_icon != NULL);

    wlr_scene_node_set_position(&drag->scene_tree->node, x, y);
}
