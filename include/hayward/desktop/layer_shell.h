#ifndef HWD_DESKTOP_LAYER_SHELL_H
#define HWD_DESKTOP_LAYER_SHELL_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>

struct hwd_output;

struct hwd_layer_shell {
    struct wlr_layer_shell_v1 *layer_shell;

    struct wl_listener new_surface;
};

struct hwd_layer_surface {
    struct wlr_layer_surface_v1 *layer_surface;

    bool mapped;
    struct hwd_output *output;

    struct wlr_scene_layer_surface_v1 *scene;
    struct wlr_scene_tree *scene_tree;
    struct wlr_addon scene_tree_marker;
    struct wlr_scene_tree *popups;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
    struct wl_listener output_destroy;
    struct wl_listener node_destroy;
    struct wl_listener new_popup;
};

struct hwd_layer_popup {
    struct wlr_xdg_popup *wlr_popup;
    struct hwd_layer_surface *toplevel;

    struct wlr_scene_tree *scene;

    struct wl_listener destroy;
    struct wl_listener new_popup;
};

void
arrange_layers(struct hwd_output *output);

struct hwd_layer_shell *
hwd_layer_shell_create(struct wl_display *wl_display);

struct hwd_layer_surface *
layer_surface_for_scene_node(struct wlr_scene_node *node);

#endif
