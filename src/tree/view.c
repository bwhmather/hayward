#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/view.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward/tree/window.h>

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl) {
    view->scene_tree = wlr_scene_tree_create(NULL);
    assert(view->scene_tree != NULL);

    view->layers.content_tree = wlr_scene_tree_create(view->scene_tree);
    assert(view->layers.content_tree != NULL);

    view->type = type;
    view->impl = impl;
    wl_signal_init(&view->events.unmap);
}

void
view_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);
    assert(view->destroying);
    assert(view->window == NULL);

    wl_list_remove(&view->events.unmap.listener_list);

    wlr_scene_node_destroy(&view->layers.content_tree->node);
    wlr_scene_node_destroy(&view->scene_tree->node);

    if (view->impl->destroy) {
        view->impl->destroy(view);
    } else {
        free(view);
    }
}

void
view_begin_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);

    // Unmapping will mark the window as dead and trigger a transaction.  It
    // isn't safe to fully destroy the window until this transaction has
    // completed.  Setting `view->destroying` will tell the window to clean up
    // the view once it has finished cleaning up itself.
    view->destroying = true;
    if (!view->window) {
        view_destroy(view);
    }
}

void
view_set_activated(struct hwd_view *view, bool activated) {
    if (view->impl->set_activated) {
        view->impl->set_activated(view, activated);
    }
}

void
view_close_popups(struct hwd_view *view) {
    if (view->impl->close_popups) {
        view->impl->close_popups(view);
    }
}

void
view_center_surface(struct hwd_view *view) {
    struct hwd_window *window = view->window;

    // We always center the current coordinates rather than the next, as the
    // geometry immediately affects the currently active rendering.
    int x = (int)fmax(0, (window->committed.content_width - view->geometry.width) / 2);
    int y = (int)fmax(0, (window->committed.content_height - view->geometry.height) / 2);
    int width = (int)window->committed.content_width;
    int height = (int)window->committed.content_height;

    wlr_scene_node_set_position(&view->layers.content_tree->node, x, y);
    if (!wl_list_empty(&view->layers.content_tree->children)) {
        wlr_scene_subsurface_tree_set_clip(
            &view->layers.content_tree->node,
            &(struct wlr_box){.x = x, .y = y, .width = width, .height = height}
        );
    }
}

static void
send_frame_done_iterator(struct wlr_scene_buffer *scene_buffer, int x, int y, void *data) {
    struct timespec *when = data;
    wl_signal_emit_mutable(&scene_buffer->events.frame_done, when);
}

void
view_send_frame_done(struct hwd_view *view) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    struct wlr_scene_node *node;
    wl_list_for_each(node, &view->layers.content_tree->children, link) {
        wlr_scene_node_for_each_buffer(node, send_frame_done_iterator, &now);
    }
}
