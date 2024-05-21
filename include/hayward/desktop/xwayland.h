#ifndef HWD_DESKTOP_XWAYLAND_H
#define HWD_DESKTOP_XWAYLAND_H

#include <config.h>

#include <stdbool.h>
#include <xcb/xproto.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>
#include <wlr/xwayland/xwayland.h>

#include <hayward/tree/view.h>

#ifdef HAVE_XWAYLAND

enum atom_name {
    NET_WM_WINDOW_TYPE_NORMAL,
    NET_WM_WINDOW_TYPE_DIALOG,
    NET_WM_WINDOW_TYPE_UTILITY,
    NET_WM_WINDOW_TYPE_TOOLBAR,
    NET_WM_WINDOW_TYPE_SPLASH,
    NET_WM_WINDOW_TYPE_MENU,
    NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
    NET_WM_WINDOW_TYPE_POPUP_MENU,
    NET_WM_WINDOW_TYPE_TOOLTIP,
    NET_WM_WINDOW_TYPE_NOTIFICATION,
    NET_WM_STATE_MODAL,
    ATOM_LAST,
};

struct hwd_xwayland {
    struct wlr_xwayland *xwayland;
    struct wlr_xcursor_manager *xcursor_manager;

    xcb_atom_t atoms[ATOM_LAST];

    struct wl_listener new_surface;
    struct wl_listener ready;
};

struct hwd_xwayland_view {
    struct hwd_view view;

    struct hwd_xwayland *xwayland;

    struct wlr_xwayland_surface *wlr_xwayland_surface;

    bool configured_is_tiled;
    bool configured_is_fullscreen;
    int configured_x;
    int configured_y;
    int configured_width;
    int configured_height;

    struct wlr_scene_tree *surface_scene;

    struct wl_listener xsurface_commit;
    struct wl_listener xsurface_request_move;
    struct wl_listener xsurface_request_resize;
    struct wl_listener xsurface_request_minimize;
    struct wl_listener xsurface_request_configure;
    struct wl_listener xsurface_request_fullscreen;
    struct wl_listener xsurface_request_activate;
    struct wl_listener xsurface_set_title;
    struct wl_listener xsurface_set_class;
    struct wl_listener xsurface_set_role;
    struct wl_listener xsurface_set_window_type;
    struct wl_listener xsurface_set_hints;
    struct wl_listener xsurface_map;
    struct wl_listener xsurface_unmap;
    struct wl_listener xsurface_associate;
    struct wl_listener xsurface_dissociate;
    struct wl_listener xsurface_destroy;
    struct wl_listener xsurface_override_redirect;
    struct wl_listener window_commit;
};

struct hwd_xwayland_unmanaged {
    struct wlr_xwayland_surface *wlr_xwayland_surface;

    struct hwd_xwayland *xwayland;

    struct wlr_scene_surface *surface_scene;
    struct wlr_addon surface_scene_marker;

    struct wl_listener xsurface_request_activate;
    struct wl_listener xsurface_request_configure;
    struct wl_listener xsurface_set_geometry;
    struct wl_listener xsurface_map;
    struct wl_listener xsurface_unmap;
    struct wl_listener xsurface_associate;
    struct wl_listener xsurface_dissociate;
    struct wl_listener xsurface_destroy;
    struct wl_listener xsurface_override_redirect;
};

struct hwd_view *
view_from_wlr_xwayland_surface(struct wlr_xwayland_surface *xsurface);

struct hwd_xwayland *
hwd_xwayland_create(struct wl_display *wl_display, struct wlr_compositor *compositor, bool lazy);
void
hwd_xwayland_destroy(struct hwd_xwayland *);

#endif // HAVE_XWAYLAND

#endif
