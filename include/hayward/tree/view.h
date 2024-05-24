#ifndef HWD_TREE_VIEW_H
#define HWD_TREE_VIEW_H

#include <config.h>

#include <stdbool.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <hayward/config.h>

struct hwd_window;
struct hwd_view;

enum hwd_view_type {
    HWD_VIEW_XDG_SHELL,
#if HAVE_XWAYLAND
    HWD_VIEW_XWAYLAND,
#endif
};

struct hwd_view_impl {
    void (*set_activated)(struct hwd_view *view, bool activated);
    void (*close_popups)(struct hwd_view *view);
    void (*destroy)(struct hwd_view *view);
};

struct hwd_view {
    enum hwd_view_type type;
    const struct hwd_view_impl *impl;

    struct wlr_scene_tree *scene_tree;

    struct {
        struct wlr_scene_tree *content_tree;
    } layers;

    struct hwd_window *window;   // NULL if unmapped and transactions finished
    struct wlr_surface *surface; // NULL for unmapped views

    // The geometry for whatever the client is committing, regardless of
    // transaction state. Updated on every commit.
    struct wlr_box geometry;

    bool destroying;

    struct {
        struct wl_signal unmap;
    } events;

    int max_render_time; // In milliseconds
};

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl);

void
view_destroy(struct hwd_view *view);

void
view_begin_destroy(struct hwd_view *view);

void
view_set_activated(struct hwd_view *view, bool activated);

void
view_close_popups(struct hwd_view *view);

void
view_update_size(struct hwd_view *view);
void
view_center_surface(struct hwd_view *view);

struct hwd_view *
view_from_wlr_surface(struct wlr_surface *surface);

void
view_send_frame_done(struct hwd_view *view);

#endif
