#ifndef HWD_TREE_VIEW_H
#define HWD_TREE_VIEW_H

#include <config.h>

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#include <wlr/xwayland/xwayland.h>

#include <hayward/config.h>

struct hwd_window;
struct hwd_view;

enum hwd_view_type {
    HWD_VIEW_XDG_SHELL,
#if HAVE_XWAYLAND
    HWD_VIEW_XWAYLAND,
#endif
};

enum hwd_view_prop {
    VIEW_PROP_TITLE,
    VIEW_PROP_APP_ID,
    VIEW_PROP_CLASS,
    VIEW_PROP_INSTANCE,
    VIEW_PROP_WINDOW_TYPE,
    VIEW_PROP_WINDOW_ROLE,
};

struct hwd_view_impl {
    void (*get_constraints)(
        struct hwd_view *view, double *min_width, double *max_width, double *min_height,
        double *max_height
    );
    const char *(*get_string_prop)(struct hwd_view *view, enum hwd_view_prop prop);
    uint32_t (*get_int_prop)(struct hwd_view *view, enum hwd_view_prop prop);
    void (*configure)(struct hwd_view *view, double lx, double ly, int width, int height);
    void (*set_activated)(struct hwd_view *view, bool activated);
    void (*set_tiled)(struct hwd_view *view, bool tiled);
    void (*set_fullscreen)(struct hwd_view *view, bool fullscreen);
    void (*set_resizing)(struct hwd_view *view, bool resizing);
    bool (*wants_floating)(struct hwd_view *view);
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

    pid_t pid;

    // The size the view would want to be if it weren't tiled.
    // Used when changing a view from tiled to floating.
    int natural_width, natural_height;

    char *title_format;

    bool using_csd;

    struct timespec urgent;
    bool allow_request_urgent;
    struct wl_event_source *urgent_timer;

    // The geometry for whatever the client is committing, regardless of
    // transaction state. Updated on every commit.
    struct wlr_box geometry;

    struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
    struct wl_listener foreign_activate_request;
    struct wl_listener foreign_fullscreen_request;
    struct wl_listener foreign_close_request;
    struct wl_listener foreign_destroy;

    bool destroying;

    union {
        struct wlr_xdg_toplevel *wlr_xdg_toplevel;
#if HAVE_XWAYLAND
        struct wlr_xwayland_surface *wlr_xwayland_surface;
#endif
    };

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
view_set_tiled(struct hwd_view *view, bool tiled);

void
view_close(struct hwd_view *view);

void
view_close_popups(struct hwd_view *view);

/**
 * Map a view, ie. make it visible in the tree.
 *
 * `fullscreen` should be set to true (and optionally `fullscreen_output`
 * should be populated) if the view should be made fullscreen immediately.
 */
void
view_map(
    struct hwd_view *view, struct wlr_surface *wlr_surface, bool fullscreen,
    struct wlr_output *fullscreen_output
);

void
view_unmap(struct hwd_view *view);

void
view_update_size(struct hwd_view *view);
void
view_center_surface(struct hwd_view *view);

struct hwd_view *
view_from_wlr_surface(struct wlr_surface *surface);

/**
 * Re-read the view's title property and update any relevant title bars.
 * The force argument makes it recreate the title bars even if the title hasn't
 * changed.
 */
void
view_update_title(struct hwd_view *view, bool force);

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
