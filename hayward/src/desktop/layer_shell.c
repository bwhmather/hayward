#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/layer_shell.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <wlr-layer-shell-unstable-v1-protocol.h>

#include <hayward/globals/root.h>
#include <hayward/globals/transaction.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/output.h>
#include <hayward/transaction.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/root.h>

#include <config.h>

#define HWD_LAYER_SHELL_VERSION 4

static void
surface_scene_marker_destroy(struct wlr_addon *addon) {
    // Intentionally left blank.
}

static const struct wlr_addon_interface scene_tree_marker_interface = {
    .name = "hwd_layer_surface", .destroy = surface_scene_marker_destroy};

struct hwd_layer_surface *
layer_surface_for_scene_node(struct wlr_scene_node *node);

static void
arrange_surface(
    struct hwd_output *output, const struct wlr_box *full_area, struct wlr_box *usable_area,
    struct wlr_scene_tree *tree
) {
    struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        struct hwd_layer_surface *layer_surface = layer_surface_for_scene_node(node);
        // Surface could be null during destruction.
        if (!layer_surface) {
            continue;
        }

        wlr_scene_layer_surface_v1_configure(layer_surface->scene, full_area, usable_area);
    }
}

void
arrange_layers(struct hwd_output *output) {
    struct wlr_box usable_area = {0};
    wlr_output_effective_resolution(output->wlr_output, &usable_area.width, &usable_area.height);
    const struct wlr_box full_area = usable_area;

    arrange_surface(output, &full_area, &usable_area, output->layers.shell_background);
    arrange_surface(output, &full_area, &usable_area, output->layers.shell_bottom);
    arrange_surface(output, &full_area, &usable_area, output->layers.shell_top);
    arrange_surface(output, &full_area, &usable_area, output->layers.shell_overlay);

    if (!wlr_box_equal(&usable_area, &output->usable_area)) {
        hwd_log(HWD_DEBUG, "Usable area changed, rearranging output");
        output->usable_area = usable_area;
        arrange_output(output);
    }
}

static struct wlr_scene_tree *
hwd_layer_get_scene(struct hwd_output *output, enum zwlr_layer_shell_v1_layer type) {
    switch (type) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return output->layers.shell_background;
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return output->layers.shell_bottom;
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return output->layers.shell_top;
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return output->layers.shell_overlay;
    }

    hwd_assert(false, "unreachable");
    return NULL;
}

static struct hwd_layer_surface *
hwd_layer_surface_create(struct wlr_scene_layer_surface_v1 *scene) {
    struct hwd_layer_surface *layer_surface = calloc(1, sizeof(struct hwd_layer_surface));
    hwd_assert(layer_surface != NULL, "Allocation failed");

    struct wlr_scene_tree *popups = wlr_scene_tree_create(root->layers.popups);
    hwd_assert(popups != NULL, "Allocation failed");

    layer_surface->scene_tree = scene->tree;
    layer_surface->scene = scene;
    layer_surface->layer_surface = scene->layer_surface;
    layer_surface->popups = popups;

    return layer_surface;
}

static struct hwd_layer_surface *
find_mapped_layer_by_client(struct wl_client *client, struct hwd_output *ignore_output) {
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *output = root->outputs->items[i];
        if (output == ignore_output) {
            continue;
        }
        // For now we'll only check the overlay layer
        struct wlr_scene_node *node;
        wl_list_for_each(node, &output->layers.shell_overlay->children, link) {
            struct hwd_layer_surface *layer_surface = layer_surface_for_scene_node(node);

            struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->layer_surface;
            struct wl_resource *resource = wlr_layer_surface->resource;
            if (wl_resource_get_client(resource) == client && wlr_layer_surface->surface->mapped) {
                return layer_surface;
            }
        }
    }
    return NULL;
}

static void
handle_output_destroy(struct wl_listener *listener, void *data) {
    struct hwd_layer_surface *layer_surface =
        wl_container_of(listener, layer_surface, output_destroy);

    hwd_transaction_manager_begin_transaction(transaction_manager);

    layer_surface->output = NULL;
    wlr_addon_finish(&layer_surface->scene_tree_marker);
    wlr_scene_node_destroy(&layer_surface->scene->tree->node);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
handle_node_destroy(struct wl_listener *listener, void *data) {
    struct hwd_layer_surface *layer_surface =
        wl_container_of(listener, layer_surface, node_destroy);

    hwd_transaction_manager_begin_transaction(transaction_manager);

    wlr_addon_finish(&layer_surface->scene_tree_marker);

    // Determine if this layer is being used by an exclusive client. If it is,
    // try and find another layer owned by this client to pass focus to.
    struct hwd_seat *seat = input_manager_get_default_seat();
    struct wl_client *client = wl_resource_get_client(layer_surface->layer_surface->resource);
    bool set_focus = seat->exclusive_client == client;
    if (set_focus) {
        struct hwd_layer_surface *consider_layer =
            find_mapped_layer_by_client(client, layer_surface->output);
        if (consider_layer) {
            root_set_focused_layer(root, consider_layer->layer_surface);
        }
    }

    if (layer_surface->output) {
        arrange_layers(layer_surface->output);
    }

    wlr_scene_node_destroy(&layer_surface->popups->node);

    wl_list_remove(&layer_surface->map.link);
    wl_list_remove(&layer_surface->unmap.link);
    wl_list_remove(&layer_surface->surface_commit.link);
    wl_list_remove(&layer_surface->node_destroy.link);
    wl_list_remove(&layer_surface->output_destroy.link);

    free(layer_surface);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
handle_surface_commit(struct wl_listener *listener, void *data) {
    struct hwd_layer_surface *layer_surface =
        wl_container_of(listener, layer_surface, surface_commit);

    hwd_transaction_manager_begin_transaction(transaction_manager);

    struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->layer_surface;
    uint32_t committed = wlr_layer_surface->current.committed;

    if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
        enum zwlr_layer_shell_v1_layer layer_type = wlr_layer_surface->current.layer;
        struct wlr_scene_tree *output_layer =
            hwd_layer_get_scene(layer_surface->output, layer_type);
        wlr_scene_node_reparent(&layer_surface->scene->tree->node, output_layer);
    }

    if (committed || wlr_layer_surface->surface->mapped != layer_surface->mapped) {
        layer_surface->mapped = wlr_layer_surface->surface->mapped;
        arrange_layers(layer_surface->output);
    }

    int lx, ly;
    wlr_scene_node_coords(&layer_surface->scene->tree->node, &lx, &ly);
    wlr_scene_node_set_position(&layer_surface->popups->node, lx, ly);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
handle_map(struct wl_listener *listener, void *data) {
    struct hwd_layer_surface *layer_surface = wl_container_of(listener, layer_surface, map);

    hwd_transaction_manager_begin_transaction(transaction_manager);

    struct wlr_layer_surface_v1 *wlr_layer_surface = layer_surface->scene->layer_surface;

    // Focus on new layer surface.
    if (wlr_layer_surface->current.keyboard_interactive &&
        (wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY ||
         wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP)) {
        root_set_focused_layer(root, wlr_layer_surface);
        arrange_layers(layer_surface->output);
    }

    cursor_rebase_all();

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
handle_unmap(struct wl_listener *listener, void *data) {
    struct hwd_layer_surface *layer_surface = wl_container_of(listener, layer_surface, unmap);

    hwd_transaction_manager_begin_transaction(transaction_manager);

    struct wlr_layer_surface_v1 *focused = root_get_focused_layer(root);
    if (layer_surface->layer_surface == focused) {
        root_set_focused_layer(root, NULL);
    }

    cursor_rebase_all();

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
popup_handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_layer_popup *popup = wl_container_of(listener, popup, destroy);

    hwd_transaction_manager_begin_transaction(transaction_manager);

    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->new_popup.link);
    free(popup);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
popup_unconstrain(struct hwd_layer_popup *popup) {
    struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;
    struct hwd_output *output = popup->toplevel->output;

    // if a client tries to create a popup while we are in the process of
    // destroying its output, don't crash.
    if (!output) {
        return;
    }

    int lx, ly;
    wlr_scene_node_coords(&popup->toplevel->scene->tree->node, &lx, &ly);

    // the output box expressed in the coordinate system of the toplevel parent
    // of the popup
    struct wlr_box output_toplevel_sx_box = {
        .x = output->lx - lx,
        .y = output->ly - ly,
        .width = output->width,
        .height = output->height,
    };

    wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static void
popup_handle_new_popup(struct wl_listener *listener, void *data);

static struct hwd_layer_popup *
create_popup(
    struct wlr_xdg_popup *wlr_popup, struct hwd_layer_surface *toplevel,
    struct wlr_scene_tree *parent
) {
    struct hwd_layer_popup *popup = calloc(1, sizeof(struct hwd_layer_popup));
    if (popup == NULL) {
        return NULL;
    }

    popup->toplevel = toplevel;
    popup->wlr_popup = wlr_popup;
    popup->scene = wlr_scene_xdg_surface_create(parent, wlr_popup->base);

    popup->destroy.notify = popup_handle_destroy;
    wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
    popup->new_popup.notify = popup_handle_new_popup;
    wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

    popup_unconstrain(popup);

    return popup;
}

static void
popup_handle_new_popup(struct wl_listener *listener, void *data) {
    struct hwd_layer_popup *hwd_layer_popup = wl_container_of(listener, hwd_layer_popup, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    hwd_transaction_manager_begin_transaction(transaction_manager);

    create_popup(wlr_popup, hwd_layer_popup->toplevel, hwd_layer_popup->scene);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
handle_new_popup(struct wl_listener *listener, void *data) {
    struct hwd_layer_surface *layer_surface = wl_container_of(listener, layer_surface, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;

    hwd_transaction_manager_begin_transaction(transaction_manager);

    create_popup(wlr_popup, layer_surface, layer_surface->popups);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

static void
handle_new_surface(struct wl_listener *listener, void *data) {
    struct wlr_layer_surface_v1 *wlr_layer_surface = data;

    hwd_transaction_manager_begin_transaction(transaction_manager);

    hwd_log(
        HWD_DEBUG,
        "new layer surface: namespace %s layer %d anchor %" PRIu32 " size %" PRIu32 "x%" PRIu32
        " margin %" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",",
        wlr_layer_surface->namespace, wlr_layer_surface->pending.layer,
        wlr_layer_surface->pending.anchor, wlr_layer_surface->pending.desired_width,
        wlr_layer_surface->pending.desired_height, wlr_layer_surface->pending.margin.top,
        wlr_layer_surface->pending.margin.right, wlr_layer_surface->pending.margin.bottom,
        wlr_layer_surface->pending.margin.left
    );

    if (!wlr_layer_surface->output) {
        // Assign last active output.
        struct hwd_output *output = root->outputs->items[0];
        wlr_layer_surface->output = output->wlr_output;
    }

    struct hwd_output *output = wlr_layer_surface->output->data;

    enum zwlr_layer_shell_v1_layer layer_type = wlr_layer_surface->pending.layer;
    struct wlr_scene_tree *output_layer = hwd_layer_get_scene(output, layer_type);
    struct wlr_scene_layer_surface_v1 *scene_surface =
        wlr_scene_layer_surface_v1_create(output_layer, wlr_layer_surface);
    if (!scene_surface) {
        hwd_log(HWD_ERROR, "Could not allocate a wlr_layer_surface_v1");
        return;
    }

    struct hwd_layer_surface *surface = hwd_layer_surface_create(scene_surface);
    hwd_assert(surface != NULL, "Allocation failed");

    wlr_addon_init(
        &surface->scene_tree_marker, &scene_surface->tree->node.addons,
        &scene_tree_marker_interface, &scene_tree_marker_interface
    );

    surface->output = output;

    surface->surface_commit.notify = handle_surface_commit;
    wl_signal_add(&wlr_layer_surface->surface->events.commit, &surface->surface_commit);
    surface->map.notify = handle_map;
    wl_signal_add(&wlr_layer_surface->surface->events.map, &surface->map);
    surface->unmap.notify = handle_unmap;
    wl_signal_add(&wlr_layer_surface->surface->events.unmap, &surface->unmap);
    surface->new_popup.notify = handle_new_popup;
    wl_signal_add(&wlr_layer_surface->events.new_popup, &surface->new_popup);

    surface->output_destroy.notify = handle_output_destroy;
    wl_signal_add(&output->events.disable, &surface->output_destroy);

    surface->node_destroy.notify = handle_node_destroy;
    wl_signal_add(&scene_surface->tree->node.events.destroy, &surface->node_destroy);

    // Temporarily set the layer's current state to pending so that we can
    // easily arrange it.
    struct wlr_layer_surface_v1_state old_state = wlr_layer_surface->current;
    wlr_layer_surface->current = wlr_layer_surface->pending;
    arrange_layers(output);
    wlr_layer_surface->current = old_state;

    hwd_transaction_manager_end_transaction(transaction_manager);
}

struct hwd_layer_shell *
hwd_layer_shell_create(struct wl_display *wl_display) {
    struct hwd_layer_shell *layer_shell = calloc(1, sizeof(struct hwd_layer_shell));
    if (layer_shell == NULL) {
        return NULL;
    }

    layer_shell->layer_shell = wlr_layer_shell_v1_create(wl_display, HWD_LAYER_SHELL_VERSION);
    if (layer_shell->layer_shell == NULL) {
        free(layer_shell);
        return NULL;
    }

    layer_shell->new_surface.notify = handle_new_surface;
    wl_signal_add(&layer_shell->layer_shell->events.new_surface, &layer_shell->new_surface);

    return layer_shell;
}

struct hwd_layer_surface *
layer_surface_for_scene_node(struct wlr_scene_node *node) {
    struct wlr_addon *addon =
        wlr_addon_find(&node->addons, &scene_tree_marker_interface, &scene_tree_marker_interface);
    if (addon == NULL) {
        return NULL;
    }

    struct hwd_layer_surface *layer_surface;
    layer_surface = wl_container_of(addon, layer_surface, scene_tree_marker);

    return layer_surface;
}
