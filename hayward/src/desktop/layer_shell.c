#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <wlr-layer-shell-unstable-v1-protocol.h>
#include <wlr/types/wlr_scene.h>

#include <hayward/desktop/transaction.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/layers.h>
#include <hayward/output.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/root.h>

#include <config.h>

static void arrange_surface(struct hayward_output *output, const struct wlr_box *full_area,
		struct wlr_box *usable_area, struct wlr_scene_tree *tree) {
	struct wlr_scene_node *node;
	wl_list_for_each(node, &tree->children, link) {
		struct hayward_layer_surface *surface = scene_descriptor_try_get(node,
			SWAY_SCENE_DESC_LAYER_SHELL);
		// surface could be null during destruction
		if (!surface) {
			continue;
		}

		wlr_scene_layer_surface_v1_configure(surface->scene, full_area, usable_area);
	}
}

static void
arrange_layer(
    struct hayward_output *output, struct wl_list *list,
    struct wlr_box *usable_area, bool exclusive
) {
    struct hayward_layer_surface *hayward_layer;
    struct wlr_box full_area = {0};
    wlr_output_effective_resolution(
        output->wlr_output, &full_area.width, &full_area.height
    );
    wl_list_for_each(hayward_layer, list, link) {
        struct wlr_layer_surface_v1 *layer = hayward_layer->layer_surface;
        struct wlr_layer_surface_v1_state *state = &layer->current;
        if (exclusive != (state->exclusive_zone > 0)) {
            continue;
        }
        struct wlr_box bounds;
        if (state->exclusive_zone == -1) {
            bounds = full_area;
        } else {
            bounds = *usable_area;
        }
        struct wlr_box box = {
            .width = state->desired_width, .height = state->desired_height};
        // Horizontal axis
        const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
        if (box.width == 0) {
            box.x = bounds.x;
        } else if ((state->anchor & both_horiz) == both_horiz) {
            box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
            box.x = bounds.x;
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
            box.x = bounds.x + (bounds.width - box.width);
        } else {
            box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
        }
        // Vertical axis
        const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
        if (box.height == 0) {
            box.y = bounds.y;
        } else if ((state->anchor & both_vert) == both_vert) {
            box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
            box.y = bounds.y;
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
            box.y = bounds.y + (bounds.height - box.height);
        } else {
            box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
        }
        // Margin
        if (box.width == 0) {
            box.x += state->margin.left;
            box.width =
                bounds.width - (state->margin.left + state->margin.right);
        } else if ((state->anchor & both_horiz) == both_horiz) {
            // don't apply margins
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
            box.x += state->margin.left;
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
            box.x -= state->margin.right;
        }
        if (box.height == 0) {
            box.y += state->margin.top;
            box.height =
                bounds.height - (state->margin.top + state->margin.bottom);
        } else if ((state->anchor & both_vert) == both_vert) {
            // don't apply margins
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
            box.y += state->margin.top;
        } else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
            box.y -= state->margin.bottom;
        }
        hayward_assert(
            box.width >= 0 && box.height >= 0,
            "Expected layer surface to have positive size"
        );

        // Apply
        hayward_layer->geo = box;
        apply_exclusive(
            usable_area, state->anchor, state->exclusive_zone,
            state->margin.top, state->margin.right, state->margin.bottom,
            state->margin.left
        );
        wlr_layer_surface_v1_configure(layer, box.width, box.height);
    }
}

void
arrange_layers(struct hayward_output *output) {
    struct wlr_box usable_area = {0};
    wlr_output_effective_resolution(
        output->wlr_output, &usable_area.width, &usable_area.height
    );

    // Arrange exclusive surfaces from top->bottom
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
        &usable_area, true
    );
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
        &usable_area, true
    );
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
        &usable_area, true
    );
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
        &usable_area, true
    );

    if (memcmp(&usable_area, &output->usable_area, sizeof(struct wlr_box)) !=
        0) {
        hayward_log(HAYWARD_DEBUG, "Usable area changed, rearranging output");
        memcpy(&output->usable_area, &usable_area, sizeof(struct wlr_box));
        arrange_output(output);
    }

    // Arrange non-exclusive surfaces from top->bottom
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
        &usable_area, false
    );
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP],
        &usable_area, false
    );
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM],
        &usable_area, false
    );
    arrange_layer(
        output, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND],
        &usable_area, false
    );

    // Find topmost keyboard interactive layer, if such a layer exists
    uint32_t layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    size_t nlayers = sizeof(layers_above_shell) / sizeof(layers_above_shell[0]);
    struct hayward_layer_surface *layer, *topmost = NULL;
    for (size_t i = 0; i < nlayers; ++i) {
        wl_list_for_each_reverse(
            layer, &output->shell_layers[layers_above_shell[i]], link
        ) {
            if (layer->layer_surface->current.keyboard_interactive &&
                layer->layer_surface->mapped) {
                topmost = layer;
                break;
            }
        }
        if (topmost != NULL) {
            break;
        }
    }

    struct wlr_layer_surface_v1 *current_layer = root_get_focused_layer(root);

    if (topmost != NULL) {
        root_set_focused_layer(root, topmost->layer_surface);
    } else if (current_layer != NULL && !current_layer->current.keyboard_interactive) {
        root_set_focused_layer(root, NULL);
    }
}

static struct hayward_layer_surface *
find_mapped_layer_by_client(
    struct wl_client *client, struct wlr_output *ignore_output
) {
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hayward_output *output = root->outputs->items[i];
        if (output->wlr_output == ignore_output) {
            continue;
        }
        // For now we'll only check the overlay layer
        struct hayward_layer_surface *lsurface;
        wl_list_for_each(
            lsurface, &output->shell_layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY],
            link
        ) {
            struct wl_resource *resource = lsurface->layer_surface->resource;
            if (wl_resource_get_client(resource) == client &&
                lsurface->layer_surface->mapped) {
                return lsurface;
            }
        }
    }
    return NULL;
}

static void
handle_output_destroy(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *hayward_layer =
        wl_container_of(listener, hayward_layer, output_destroy);
    // Determine if this layer is being used by an exclusive client. If it is,
    // try and find another layer owned by this client to pass focus to.
    struct hayward_seat *seat = input_manager_get_default_seat();
    struct wl_client *client =
        wl_resource_get_client(hayward_layer->layer_surface->resource);
    bool set_focus = seat->exclusive_client == client;

    if (set_focus) {
        struct hayward_layer_surface *layer = find_mapped_layer_by_client(
            client, hayward_layer->layer_surface->output
        );
        if (layer) {
            root_set_focused_layer(root, layer->layer_surface);
        }
    }

    wlr_layer_surface_v1_destroy(hayward_layer->layer_surface);
}

static void
handle_surface_commit(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *layer =
        wl_container_of(listener, layer, surface_commit);
    struct wlr_layer_surface_v1 *layer_surface = layer->layer_surface;
    struct wlr_output *wlr_output = layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;
    struct wlr_box old_extent = layer->extent;

    bool layer_changed = false;
    if (layer_surface->current.committed != 0 ||
        layer->mapped != layer_surface->mapped) {
        layer->mapped = layer_surface->mapped;
        layer_changed = layer->layer != layer_surface->current.layer;
        if (layer_changed) {
            wl_list_remove(&layer->link);
            wl_list_insert(
                &output->shell_layers[layer_surface->current.layer],
                &layer->link
            );
            layer->layer = layer_surface->current.layer;
        }
        arrange_layers(output);
    }

    wlr_surface_get_extends(layer_surface->surface, &layer->extent);
    layer->extent.x += layer->geo.x;
    layer->extent.y += layer->geo.y;

    bool extent_changed =
        memcmp(&old_extent, &layer->extent, sizeof(struct wlr_box)) != 0;
    if (extent_changed || layer_changed) {
        output_damage_box(output, &old_extent);
        output_damage_surface(
            output, layer->geo.x, layer->geo.y, layer_surface->surface, true
        );
    } else {
        output_damage_surface(
            output, layer->geo.x, layer->geo.y, layer_surface->surface, false
        );
    }

    transaction_flush();
}

static void
unmap(struct hayward_layer_surface *hayward_layer) {
    if (root_get_focused_layer(root) == hayward_layer->layer_surface) {
        root_set_focused_layer(root, NULL);
    }

    cursor_rebase_all();

    struct wlr_output *wlr_output = hayward_layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;
    output_damage_surface(
        output, hayward_layer->geo.x, hayward_layer->geo.y,
        hayward_layer->layer_surface->surface, true
    );
}

static void
layer_subsurface_destroy(struct hayward_layer_subsurface *subsurface);

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *hayward_layer =
        wl_container_of(listener, hayward_layer, destroy);
    hayward_log(
        HAYWARD_DEBUG, "Layer surface destroyed (%s)",
        hayward_layer->layer_surface->namespace
    );
    if (hayward_layer->layer_surface->mapped) {
        unmap(hayward_layer);
    }

    struct hayward_layer_subsurface *subsurface, *subsurface_tmp;
    wl_list_for_each_safe(
        subsurface, subsurface_tmp, &hayward_layer->subsurfaces, link
    ) {
        layer_subsurface_destroy(subsurface);
    }

    wl_list_remove(&hayward_layer->link);
    wl_list_remove(&hayward_layer->destroy.link);
    wl_list_remove(&hayward_layer->map.link);
    wl_list_remove(&hayward_layer->unmap.link);
    wl_list_remove(&hayward_layer->surface_commit.link);
    wl_list_remove(&hayward_layer->new_popup.link);
    wl_list_remove(&hayward_layer->new_subsurface.link);

    struct wlr_output *wlr_output = hayward_layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;
    arrange_layers(output);
    transaction_flush();
    wl_list_remove(&hayward_layer->output_destroy.link);
    hayward_layer->layer_surface->output = NULL;

    free(hayward_layer);
}

static void
handle_map(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *hayward_layer =
        wl_container_of(listener, hayward_layer, map);
    struct wlr_output *wlr_output = hayward_layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;
    output_damage_surface(
        output, hayward_layer->geo.x, hayward_layer->geo.y,
        hayward_layer->layer_surface->surface, true
    );
    wlr_surface_send_enter(
        hayward_layer->layer_surface->surface,
        hayward_layer->layer_surface->output
    );
    cursor_rebase_all();
}

static void
handle_unmap(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *hayward_layer =
        wl_container_of(listener, hayward_layer, unmap);
    unmap(hayward_layer);
}

static void
subsurface_damage(struct hayward_layer_subsurface *subsurface, bool whole) {
    struct hayward_layer_surface *layer = subsurface->layer_surface;
    struct wlr_output *wlr_output = layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;
    int ox = subsurface->wlr_subsurface->current.x + layer->geo.x;
    int oy = subsurface->wlr_subsurface->current.y + layer->geo.y;
    output_damage_surface(
        output, ox, oy, subsurface->wlr_subsurface->surface, whole
    );
}

static void
subsurface_handle_unmap(struct wl_listener *listener, void *data) {
    struct hayward_layer_subsurface *subsurface =
        wl_container_of(listener, subsurface, unmap);
    subsurface_damage(subsurface, true);
}

static void
subsurface_handle_map(struct wl_listener *listener, void *data) {
    struct hayward_layer_subsurface *subsurface =
        wl_container_of(listener, subsurface, map);
    subsurface_damage(subsurface, true);
}

static void
subsurface_handle_commit(struct wl_listener *listener, void *data) {
    struct hayward_layer_subsurface *subsurface =
        wl_container_of(listener, subsurface, commit);
    subsurface_damage(subsurface, false);
}

static void
layer_subsurface_destroy(struct hayward_layer_subsurface *subsurface) {
    wl_list_remove(&subsurface->link);
    wl_list_remove(&subsurface->map.link);
    wl_list_remove(&subsurface->unmap.link);
    wl_list_remove(&subsurface->destroy.link);
    wl_list_remove(&subsurface->commit.link);
    free(subsurface);
}

static void
subsurface_handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_layer_subsurface *subsurface =
        wl_container_of(listener, subsurface, destroy);
    layer_subsurface_destroy(subsurface);
}

static struct hayward_layer_subsurface *
create_subsurface(
    struct wlr_subsurface *wlr_subsurface,
    struct hayward_layer_surface *layer_surface
) {
    struct hayward_layer_subsurface *subsurface =
        calloc(1, sizeof(struct hayward_layer_subsurface));
    if (subsurface == NULL) {
        return NULL;
    }

    subsurface->wlr_subsurface = wlr_subsurface;
    subsurface->layer_surface = layer_surface;
    wl_list_insert(&layer_surface->subsurfaces, &subsurface->link);

    subsurface->map.notify = subsurface_handle_map;
    wl_signal_add(&wlr_subsurface->events.map, &subsurface->map);
    subsurface->unmap.notify = subsurface_handle_unmap;
    wl_signal_add(&wlr_subsurface->events.unmap, &subsurface->unmap);
    subsurface->destroy.notify = subsurface_handle_destroy;
    wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
    subsurface->commit.notify = subsurface_handle_commit;
    wl_signal_add(&wlr_subsurface->surface->events.commit, &subsurface->commit);

    return subsurface;
}

static void
handle_new_subsurface(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *hayward_layer_surface =
        wl_container_of(listener, hayward_layer_surface, new_subsurface);
    struct wlr_subsurface *wlr_subsurface = data;
    create_subsurface(wlr_subsurface, hayward_layer_surface);
}

static struct hayward_layer_surface *
popup_get_layer(struct hayward_layer_popup *popup) {
    while (popup->parent_type == LAYER_PARENT_POPUP) {
        popup = popup->parent_popup;
    }
    return popup->parent_layer;
}

static void
popup_damage(struct hayward_layer_popup *layer_popup, bool whole) {
    struct wlr_xdg_popup *popup = layer_popup->wlr_popup;
    struct wlr_surface *surface = popup->base->surface;
    int popup_sx = popup->current.geometry.x - popup->base->current.geometry.x;
    int popup_sy = popup->current.geometry.y - popup->base->current.geometry.y;
    int ox = popup_sx, oy = popup_sy;
    struct hayward_layer_surface *layer;
    while (true) {
        if (layer_popup->parent_type == LAYER_PARENT_POPUP) {
            layer_popup = layer_popup->parent_popup;
            ox += layer_popup->wlr_popup->current.geometry.x;
            oy += layer_popup->wlr_popup->current.geometry.y;
        } else {
            layer = layer_popup->parent_layer;
            ox += layer->geo.x;
            oy += layer->geo.y;
            break;
        }
    }
    struct wlr_output *wlr_output = layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;
    output_damage_surface(output, ox, oy, surface, whole);
}

static void
popup_handle_map(struct wl_listener *listener, void *data) {
    struct hayward_layer_popup *popup = wl_container_of(listener, popup, map);
    struct hayward_layer_surface *layer = popup_get_layer(popup);
    struct wlr_output *wlr_output = layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    wlr_surface_send_enter(popup->wlr_popup->base->surface, wlr_output);
    popup_damage(popup, true);
}

static void
popup_handle_unmap(struct wl_listener *listener, void *data) {
    struct hayward_layer_popup *popup = wl_container_of(listener, popup, unmap);
    popup_damage(popup, true);
}

static void
popup_handle_commit(struct wl_listener *listener, void *data) {
    struct hayward_layer_popup *popup =
        wl_container_of(listener, popup, commit);
    popup_damage(popup, false);
}

static void
popup_handle_destroy(struct wl_listener *listener, void *data) {
    struct hayward_layer_popup *popup =
        wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->destroy.link);
    wl_list_remove(&popup->commit.link);
    free(popup);
}

static void
popup_unconstrain(struct hayward_layer_popup *popup) {
    struct hayward_layer_surface *layer = popup_get_layer(popup);
    struct wlr_xdg_popup *wlr_popup = popup->wlr_popup;

    struct wlr_output *wlr_output = layer->layer_surface->output;
    hayward_assert(wlr_output, "wlr_layer_surface_v1 has null output");
    struct hayward_output *output = wlr_output->data;

    // the output box expressed in the coordinate system of the toplevel parent
    // of the popup
    struct wlr_box output_toplevel_sx_box = {
        .x = -layer->geo.x,
        .y = -layer->geo.y,
        .width = output->width,
        .height = output->height,
    };

    wlr_xdg_popup_unconstrain_from_box(wlr_popup, &output_toplevel_sx_box);
}

static void
popup_handle_new_popup(struct wl_listener *listener, void *data);

static struct hayward_layer_popup *
create_popup(
    struct wlr_xdg_popup *wlr_popup, enum layer_parent parent_type, void *parent
) {
    struct hayward_layer_popup *popup =
        calloc(1, sizeof(struct hayward_layer_popup));
    if (popup == NULL) {
        return NULL;
    }

    popup->wlr_popup = wlr_popup;
    popup->parent_type = parent_type;
    popup->parent_layer = parent;

    popup->map.notify = popup_handle_map;
    wl_signal_add(&wlr_popup->base->events.map, &popup->map);
    popup->unmap.notify = popup_handle_unmap;
    wl_signal_add(&wlr_popup->base->events.unmap, &popup->unmap);
    popup->destroy.notify = popup_handle_destroy;
    wl_signal_add(&wlr_popup->base->events.destroy, &popup->destroy);
    popup->commit.notify = popup_handle_commit;
    wl_signal_add(&wlr_popup->base->surface->events.commit, &popup->commit);
    popup->new_popup.notify = popup_handle_new_popup;
    wl_signal_add(&wlr_popup->base->events.new_popup, &popup->new_popup);

    popup_unconstrain(popup);

    return popup;
}

static void
popup_handle_new_popup(struct wl_listener *listener, void *data) {
    struct hayward_layer_popup *hayward_layer_popup =
        wl_container_of(listener, hayward_layer_popup, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;
    create_popup(wlr_popup, LAYER_PARENT_POPUP, hayward_layer_popup);
}

static void
handle_new_popup(struct wl_listener *listener, void *data) {
    struct hayward_layer_surface *hayward_layer_surface =
        wl_container_of(listener, hayward_layer_surface, new_popup);
    struct wlr_xdg_popup *wlr_popup = data;
    create_popup(wlr_popup, LAYER_PARENT_LAYER, hayward_layer_surface);
}

void
handle_layer_shell_surface(struct wl_listener *listener, void *data) {
    struct wlr_layer_surface_v1 *layer_surface = data;
    hayward_log(
        HAYWARD_DEBUG,
        "new layer surface: namespace %s layer %d anchor %" PRIu32
        " size %" PRIu32 "x%" PRIu32 " margin %" PRIu32 ",%" PRIu32 ",%" PRIu32
        ",%" PRIu32 ",",
        layer_surface->namespace, layer_surface->pending.layer,
        layer_surface->pending.anchor, layer_surface->pending.desired_width,
        layer_surface->pending.desired_height,
        layer_surface->pending.margin.top, layer_surface->pending.margin.right,
        layer_surface->pending.margin.bottom, layer_surface->pending.margin.left
    );

    if (!layer_surface->output) {
        // Assign last active output
        struct hayward_output *output = NULL;
        if (!root->outputs->length) {
            hayward_log(
                HAYWARD_ERROR, "no output to auto-assign layer surface '%s' to",
                layer_surface->namespace
            );
            // Note that layer_surface->output can be NULL
            // here, but none of our destroy callbacks are
            // registered yet so we don't have to make them
            // handle that case.
            wlr_layer_surface_v1_destroy(layer_surface);
            return;
        }
        output = root->outputs->items[0];
        layer_surface->output = output->wlr_output;
    }

    struct hayward_layer_surface *hayward_layer =
        calloc(1, sizeof(struct hayward_layer_surface));
    if (!hayward_layer) {
        return;
    }

    wl_list_init(&hayward_layer->subsurfaces);

    hayward_layer->surface_commit.notify = handle_surface_commit;
    wl_signal_add(
        &layer_surface->surface->events.commit, &hayward_layer->surface_commit
    );

    hayward_layer->destroy.notify = handle_destroy;
    wl_signal_add(&layer_surface->events.destroy, &hayward_layer->destroy);
    hayward_layer->map.notify = handle_map;
    wl_signal_add(&layer_surface->events.map, &hayward_layer->map);
    hayward_layer->unmap.notify = handle_unmap;
    wl_signal_add(&layer_surface->events.unmap, &hayward_layer->unmap);
    hayward_layer->new_popup.notify = handle_new_popup;
    wl_signal_add(&layer_surface->events.new_popup, &hayward_layer->new_popup);
    hayward_layer->new_subsurface.notify = handle_new_subsurface;
    wl_signal_add(
        &layer_surface->surface->events.new_subsurface,
        &hayward_layer->new_subsurface
    );

    hayward_layer->layer_surface = layer_surface;
    layer_surface->data = hayward_layer;

    struct hayward_output *output = layer_surface->output->data;
    hayward_layer->output_destroy.notify = handle_output_destroy;
    wl_signal_add(&output->events.disable, &hayward_layer->output_destroy);

    wl_list_insert(
        &output->shell_layers[layer_surface->pending.layer],
        &hayward_layer->link
    );

    // Temporarily set the layer's current state to pending
    // So that we can easily arrange it
    struct wlr_layer_surface_v1_state old_state = layer_surface->current;
    layer_surface->current = layer_surface->pending;
    arrange_layers(output);
    layer_surface->current = old_state;
}
