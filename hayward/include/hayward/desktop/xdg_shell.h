#ifndef HWD_DESKTOP_XDG_SHELL_H
#define HWD_DESKTOP_XDG_SHELL_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include <hayward/tree/view.h>

struct hwd_xdg_shell {
    struct wlr_xdg_shell *xdg_shell;

    struct wl_listener new_surface;
};

struct hwd_xdg_shell_view {
    struct hwd_view view;

    struct hwd_xdg_shell *xdg_shell;

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

struct hwd_xdg_popup {
    struct hwd_view *view;

    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_tree *xdg_surface_tree;
    struct wlr_xdg_popup *wlr_xdg_popup;

    struct wl_listener new_popup;
    struct wl_listener destroy;
};

struct hwd_view *
view_from_wlr_xdg_surface(struct wlr_xdg_surface *xdg_surface);

struct hwd_xdg_shell *
hwd_xdg_shell_create(struct wl_display *wl_display);

#endif
