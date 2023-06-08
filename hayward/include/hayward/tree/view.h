#ifndef HAYWARD_TREE_VIEW_H
#define HAYWARD_TREE_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

#include <hayward/config.h>

#include <config.h>

#if HAVE_XWAYLAND
#include <wlr/xwayland/xwayland.h>
#endif

struct hayward_window;
struct hayward_view;
struct hayward_xdg_decoration;

enum hayward_view_type {
    HAYWARD_VIEW_XDG_SHELL,
#if HAVE_XWAYLAND
    HAYWARD_VIEW_XWAYLAND,
#endif
};

enum hayward_view_prop {
    VIEW_PROP_TITLE,
    VIEW_PROP_APP_ID,
    VIEW_PROP_CLASS,
    VIEW_PROP_INSTANCE,
    VIEW_PROP_WINDOW_TYPE,
    VIEW_PROP_WINDOW_ROLE,
#if HAVE_XWAYLAND
    VIEW_PROP_X11_WINDOW_ID,
    VIEW_PROP_X11_PARENT_ID,
#endif
};

struct hayward_view_impl {
    void (*get_constraints
    )(struct hayward_view *view, double *min_width, double *max_width,
      double *min_height, double *max_height);
    const char *(*get_string_prop
    )(struct hayward_view *view, enum hayward_view_prop prop);
    uint32_t (*get_int_prop
    )(struct hayward_view *view, enum hayward_view_prop prop);
    uint32_t (*configure
    )(struct hayward_view *view, double lx, double ly, int width, int height);
    void (*set_activated)(struct hayward_view *view, bool activated);
    void (*set_tiled)(struct hayward_view *view, bool tiled);
    void (*set_fullscreen)(struct hayward_view *view, bool fullscreen);
    void (*set_resizing)(struct hayward_view *view, bool resizing);
    bool (*wants_floating)(struct hayward_view *view);
    bool (*is_transient_for
    )(struct hayward_view *child, struct hayward_view *ancestor);
    void (*close)(struct hayward_view *view);
    void (*close_popups)(struct hayward_view *view);
    void (*destroy)(struct hayward_view *view);
};

struct hayward_view {
    enum hayward_view_type type;
    const struct hayward_view_impl *impl;

    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_tree *content_tree;
    struct wlr_scene_tree *saved_surface_tree;

    struct hayward_window *window; // NULL if unmapped and transactions finished
    struct wlr_surface *surface;   // NULL for unmapped views
    struct hayward_xdg_decoration *xdg_decoration;

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

    // The "old" geometry during a transaction. Used to damage the old location
    // when a transaction is applied.
    struct wlr_box saved_geometry;

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

struct hayward_xdg_shell_view {
    struct hayward_view view;

    struct wl_listener commit;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_fullscreen;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
    struct wl_listener new_popup;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};
#if HAVE_XWAYLAND
struct hayward_xwayland_view {
    struct hayward_view view;

    struct wlr_scene_surface *surface_scene;

    struct wl_listener commit;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_minimize;
    struct wl_listener request_configure;
    struct wl_listener request_fullscreen;
    struct wl_listener request_activate;
    struct wl_listener set_title;
    struct wl_listener set_class;
    struct wl_listener set_role;
    struct wl_listener set_window_type;
    struct wl_listener set_hints;
    struct wl_listener set_decorations;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener override_redirect;
};

struct hayward_xwayland_unmanaged {
    struct wlr_xwayland_surface *wlr_xwayland_surface;

    struct wlr_scene_surface *surface_scene;
    struct wlr_addon surface_scene_marker;

    struct wl_listener request_activate;
    struct wl_listener request_configure;
    struct wl_listener request_fullscreen;
    struct wl_listener set_geometry;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener override_redirect;
};
#endif

struct hayward_xdg_popup {
    struct hayward_view *view;

    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_tree *xdg_surface_tree;
    struct wlr_xdg_popup *wlr_xdg_popup;

    struct wl_listener new_popup;
    struct wl_listener destroy;
};

void
view_init(
    struct hayward_view *view, enum hayward_view_type type,
    const struct hayward_view_impl *impl
);

void
view_destroy(struct hayward_view *view);

void
view_begin_destroy(struct hayward_view *view);

const char *
view_get_title(struct hayward_view *view);

const char *
view_get_app_id(struct hayward_view *view);

const char *
view_get_class(struct hayward_view *view);

const char *
view_get_instance(struct hayward_view *view);

uint32_t
view_get_x11_window_id(struct hayward_view *view);

uint32_t
view_get_x11_parent_id(struct hayward_view *view);

const char *
view_get_window_role(struct hayward_view *view);

uint32_t
view_get_window_type(struct hayward_view *view);

const char *
view_get_shell(struct hayward_view *view);

void
view_get_constraints(
    struct hayward_view *view, double *min_width, double *max_width,
    double *min_height, double *max_height
);

uint32_t
view_configure(
    struct hayward_view *view, double lx, double ly, int width, int height
);

bool
view_inhibit_idle(struct hayward_view *view);

void
view_set_activated(struct hayward_view *view, bool activated);

/**
 * Called when the view requests to be focused.
 */
void
view_request_activate(struct hayward_view *view);

/**
 * If possible, instructs the client to change their decoration mode.
 */
void
view_set_csd_from_server(struct hayward_view *view, bool enabled);

/**
 * Updates the view's border setting when the client unexpectedly changes their
 * decoration mode.
 */
void
view_update_csd_from_client(struct hayward_view *view, bool enabled);

void
view_set_tiled(struct hayward_view *view, bool tiled);

void
view_close(struct hayward_view *view);

void
view_close_popups(struct hayward_view *view);

/**
 * Map a view, ie. make it visible in the tree.
 *
 * `fullscreen` should be set to true (and optionally `fullscreen_output`
 * should be populated) if the view should be made fullscreen immediately.
 *
 * `decoration` should be set to true if the client prefers CSD. The client's
 * preference may be ignored.
 */
void
view_map(
    struct hayward_view *view, struct wlr_surface *wlr_surface, bool fullscreen,
    struct wlr_output *fullscreen_output, bool decoration
);

void
view_unmap(struct hayward_view *view);

void
view_update_size(struct hayward_view *view);
void
view_center_surface(struct hayward_view *view);

struct hayward_view *
view_from_wlr_xdg_surface(struct wlr_xdg_surface *xdg_surface);
#if HAVE_XWAYLAND
struct hayward_view *
view_from_wlr_xwayland_surface(struct wlr_xwayland_surface *xsurface);
#endif
struct hayward_view *
view_from_wlr_surface(struct wlr_surface *surface);

/**
 * Re-read the view's title property and update any relevant title bars.
 * The force argument makes it recreate the title bars even if the title hasn't
 * changed.
 */
void
view_update_title(struct hayward_view *view, bool force);

/**
 * Returns true if there's a possibility the view may be rendered on screen.
 * Intended for damage tracking.
 */
bool
view_is_visible(struct hayward_view *view);

void
view_set_urgent(struct hayward_view *view, bool enable);

bool
view_is_urgent(struct hayward_view *view);

void
view_remove_saved_buffer(struct hayward_view *view);

void
view_save_buffer(struct hayward_view *view);

bool
view_is_transient_for(
    struct hayward_view *child, struct hayward_view *ancestor
);

#endif
