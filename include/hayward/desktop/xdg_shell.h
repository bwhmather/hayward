#ifndef HWD_DESKTOP_XDG_SHELL_H
#define HWD_DESKTOP_XDG_SHELL_H

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <hayward/tree/view.h>

struct hwd_xdg_shell {
    struct wlr_xdg_shell *xdg_shell;

    struct wl_listener new_toplevel;
};

struct hwd_xdg_shell_view {
    struct hwd_view view;

    struct hwd_xdg_shell *xdg_shell;

    struct wlr_xdg_toplevel *wlr_xdg_toplevel;

    // Set to true if toplevel has received a request that the protocol requires
    // a response to.
    bool force_reconfigure;

    bool configured_is_tiled;
    bool configured_is_fullscreen;
    int configured_width;
    int configured_height;

    // Identifier tracking the serial of the configure event sent during at the
    // beginning of the current commit.  Used to discard responses for previous
    // configures.
    uint32_t configure_serial;

    struct wl_listener wlr_surface_commit;
    struct wl_listener wlr_toplevel_request_move;
    struct wl_listener wlr_toplevel_request_resize;
    struct wl_listener wlr_toplevel_request_fullscreen;
    struct wl_listener wlr_toplevel_set_title;
    struct wl_listener wlr_toplevel_set_app_id;
    struct wl_listener wlr_toplevel_set_parent;
    struct wl_listener xdg_surface_new_popup;
    struct wl_listener xdg_surface_map;
    struct wl_listener xdg_surface_unmap;
    struct wl_listener xdg_surface_destroy;
    struct wl_listener window_commit;
};

struct hwd_xdg_popup {
    struct hwd_view *view;

    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_tree *xdg_surface_tree;
    struct wlr_xdg_popup *wlr_xdg_popup;

    struct wl_listener wlr_surface_commit;
    struct wl_listener xdg_surface_new_popup;
    struct wl_listener xdg_surface_destroy;
};

struct hwd_xdg_shell_view *
hwd_xdg_shell_view_from_wlr_surface(struct wlr_surface *surface);

struct hwd_xdg_shell *
hwd_xdg_shell_create(struct wl_display *wl_display);

#endif
