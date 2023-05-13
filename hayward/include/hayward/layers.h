#ifndef _HAYWARD_LAYERS_H
#define _HAYWARD_LAYERS_H
#include <stdbool.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/util/box.h>

#include <wlr-layer-shell-unstable-v1-protocol.h>

struct hayward_layer_surface {
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
    struct wl_listener output_destroy;
    struct wl_listener node_destroy;
    struct wl_listener new_popup;

    bool mapped;

    struct sway_output *output;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree *popups;
    struct wlr_layer_surface_v1 *layer_surface;
};

struct hayward_layer_popup {
    struct wlr_xdg_popup *wlr_popup;

    struct wlr_scene_tree *scene;
    struct sway_layer_surface *toplevel;

    struct wl_listener destroy;
    struct wl_listener new_popup;
};

struct hayward_output;
void
arrange_layers(struct hayward_output *output);

#endif
