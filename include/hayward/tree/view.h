#ifndef HWD_TREE_VIEW_H
#define HWD_TREE_VIEW_H

#include <config.h>

#include <stdbool.h>
#include <time.h>

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
    void (*get_constraints)(
        struct hwd_view *view, double *min_width, double *max_width, double *min_height,
        double *max_height
    );
    void (*configure)(struct hwd_view *view, double lx, double ly, int width, int height);
    void (*set_activated)(struct hwd_view *view, bool activated);
    void (*set_resizing)(struct hwd_view *view, bool resizing);
    bool (*is_transient_for)(struct hwd_view *child, struct hwd_view *ancestor);
    void (*close)(struct hwd_view *view);
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

    // The size the view would want to be if it weren't tiled.
    // Used when changing a view from tiled to floating.
    int natural_width, natural_height;

    bool using_csd;

    struct timespec urgent;
    bool allow_request_urgent;
    struct wl_event_source *urgent_timer;

    // The geometry for whatever the client is committing, regardless of
    // transaction state. Updated on every commit.
    struct wlr_box geometry;

    bool destroying;

    struct {
        struct wl_signal unmap;
    } events;

    int max_render_time; // In milliseconds

    enum seat_config_shortcuts_inhibit shortcuts_inhibit;
};

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl);

void
view_destroy(struct hwd_view *view);

void
view_begin_destroy(struct hwd_view *view);

void
view_get_constraints(
    struct hwd_view *view, double *min_width, double *max_width, double *min_height,
    double *max_height
);

void
view_configure(struct hwd_view *view, double lx, double ly, int width, int height);

void
view_set_activated(struct hwd_view *view, bool activated);

/**
 * Called when the view requests to be focused.
 */
void
view_request_activate(struct hwd_view *view);

void
view_close(struct hwd_view *view);

void
view_close_popups(struct hwd_view *view);

void
view_update_size(struct hwd_view *view);
void
view_center_surface(struct hwd_view *view);

struct hwd_view *
view_from_wlr_surface(struct wlr_surface *surface);

/**
 * Returns true if there's a possibility the view may be rendered on screen.
 * Intended for damage tracking.
 */
bool
view_is_visible(struct hwd_view *view);

void
view_set_urgent(struct hwd_view *view, bool enable);

bool
view_is_urgent(struct hwd_view *view);

bool
view_is_transient_for(struct hwd_view *child, struct hwd_view *ancestor);

void
view_send_frame_done(struct hwd_view *view);

#endif
