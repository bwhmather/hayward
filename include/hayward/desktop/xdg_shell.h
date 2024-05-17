#ifndef HWD_DESKTOP_XDG_SHELL_H
#define HWD_DESKTOP_XDG_SHELL_H

#include <stdint.h>

#include <wayland-server-core.h>

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
    struct wl_listener xdg_surface_new_popup;
    struct wl_listener xdg_surface_map;
    struct wl_listener xdg_surface_unmap;
    struct wl_listener xdg_surface_destroy;
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

struct hwd_view *
view_from_wlr_xdg_surface(struct wlr_xdg_surface *xdg_surface);

struct hwd_xdg_shell *
hwd_xdg_shell_create(struct wl_display *wl_display);

#endif
