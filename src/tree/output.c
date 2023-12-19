#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/output.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/headless.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/desktop/layer_shell.h>
#include <hayward/globals/root.h>
#include <hayward/input/input_manager.h>
#include <hayward/list.h>
#include <hayward/scheduler.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

static void
output_destroy(struct hwd_output *output);

static bool
output_is_alive(struct hwd_output *output);

static void
output_init_scene(struct hwd_output *output) {
    output->scene_tree = wlr_scene_tree_create(root->layers.outputs);
    output->layers.shell_background = wlr_scene_tree_create(output->scene_tree);
    output->layers.shell_bottom = wlr_scene_tree_create(output->scene_tree);
    output->layers.fullscreen = wlr_scene_tree_create(output->scene_tree);
    output->layers.shell_top = wlr_scene_tree_create(output->scene_tree);
    output->layers.shell_overlay = wlr_scene_tree_create(output->scene_tree);
}

static void
output_update_scene(struct hwd_output *output) {
    struct hwd_window *fullscreen_window = output->committed.fullscreen_window;

    if (!wl_list_empty(&output->layers.fullscreen->children)) {
        struct wl_list *link = output->layers.fullscreen->children.next;
        struct wlr_scene_node *node = wl_container_of(link, node, link);

        if (fullscreen_window == NULL || node != &fullscreen_window->scene_tree->node) {
            wlr_scene_node_reparent(node, root->orphans);
        }
    }

    if (fullscreen_window != NULL) {
        wlr_scene_node_reparent(&fullscreen_window->scene_tree->node, output->layers.fullscreen);
    }
}

static void
output_destroy_scene(struct hwd_output *output) {
    wlr_scene_node_destroy(&output->scene_tree->node);
}

static void
output_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, transaction_commit);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    wl_list_remove(&listener->link);
    output->dirty = false;

    wl_signal_add(&transaction_manager->events.apply, &output->transaction_apply);

    memcpy(&output->committed, &output->pending, sizeof(struct hwd_output_state));
}

static void
output_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, transaction_apply);
    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    wl_list_remove(&listener->link);

    output_update_scene(output);

    if (output->committed.dead) {
        wl_signal_add(&transaction_manager->events.after_apply, &output->transaction_after_apply);
    }

    memcpy(&output->current, &output->committed, sizeof(struct hwd_output_state));
}

static void
output_handle_transaction_after_apply(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, transaction_after_apply);

    wl_list_remove(&listener->link);

    assert(output->current.dead);
    output_destroy(output);
}

struct hwd_output *
output_create(struct wlr_output *wlr_output) {
    struct hwd_output *output = calloc(1, sizeof(struct hwd_output));

    static size_t next_id = 1;
    output->id = next_id++;

    wl_signal_init(&output->events.begin_destroy);

    output->wlr_output = wlr_output;
    wlr_output->data = output;
    output->detected_subpixel = wlr_output->subpixel;
    output->scale_filter = SCALE_FILTER_NEAREST;

    output_init_scene(output);

    output->transaction_commit.notify = output_handle_transaction_commit;
    output->transaction_apply.notify = output_handle_transaction_apply;
    output->transaction_after_apply.notify = output_handle_transaction_after_apply;

    wl_signal_init(&output->events.disable);

    wl_list_insert(&root->all_outputs, &output->link);

    size_t len = sizeof(output->shell_layers) / sizeof(output->shell_layers[0]);
    for (size_t i = 0; i < len; ++i) {
        wl_list_init(&output->shell_layers[i]);
    }

    return output;
}

static bool
output_is_alive(struct hwd_output *output) {
    assert(output != NULL);
    return !output->pending.dead;
}

static void
output_set_dirty(struct hwd_output *output) {
    assert(output != NULL);

    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    if (output->dirty) {
        return;
    }

    output->dirty = true;
    wl_signal_add(&transaction_manager->events.commit, &output->transaction_commit);
    hwd_transaction_manager_ensure_queued(transaction_manager);
}

static void
output_enable(struct hwd_output *output) {
    if (output->enabled) {
        return;
    }
    output->enabled = true;
    list_add(root->outputs, output);
    if (root->pending.active_output == NULL) {
        root->pending.active_output = output;
    }

    input_manager_configure_xcursor();

    arrange_layers(output);
    root_arrange(root);
}

static void
output_evacuate(struct hwd_output *output) {
    struct hwd_output *new_output = NULL;
    if (root->outputs->length > 1) {
        new_output = root->outputs->items[0];
        if (new_output == output) {
            new_output = root->outputs->items[1];
        }
    }

    for (int i = 0; i < root->pending.workspaces->length; i++) {
        struct hwd_workspace *workspace = root->pending.workspaces->items[i];

        // Move tiling windows.
        for (int j = 0; j < workspace->pending.columns->length; j++) {
            struct hwd_column *column = workspace->pending.columns->items[j];

            if (column->pending.output != output) {
                continue;
            }

            column->pending.output = new_output;
            for (int k = 0; k < column->pending.children->length; k++) {
                struct hwd_window *window = column->pending.children->items[k];

                window->pending.fullscreen = false;
                window->pending.output = output;
            }
        }

        for (int j = 0; j < workspace->pending.floating->length; j++) {
            struct hwd_window *window = workspace->pending.floating->items[j];

            if (window->pending.output != output) {
                continue;
            }

            window->pending.fullscreen = false;
            window->pending.output = output;

            window_floating_move_to_center(window);
        }

        workspace_arrange(workspace);
    }
}

static void
output_destroy(struct hwd_output *output) {
    assert(output->current.dead);
    assert(output->wlr_output == NULL);
    assert(!output->dirty);

    output_destroy_scene(output);

    free(output);
}

static void
output_disable(struct hwd_output *output) {
    assert(output->enabled);

    int index = list_find(root->outputs, output);
    assert(index >= 0);

    wlr_log(WLR_DEBUG, "Disabling output '%s'", output->wlr_output->name);
    wl_signal_emit_mutable(&output->events.disable, output);

    output_evacuate(output);

    list_del(root->outputs, index);
    if (root->pending.active_output == output) {
        if (root->outputs->length == 0) {
            root->pending.active_output = NULL;
        } else {
            root->pending.active_output = root->outputs->items[index - 1 < 0 ? 0 : index - 1];
        }
    }

    output->enabled = false;
    output->current_mode = NULL;

    root_arrange(root);

    // Reconfigure all devices, since devices with map_to_output directives for
    // an output that goes offline should stop sending events as long as the
    // output remains offline.
    input_manager_configure_all_inputs();
}

static void
output_begin_destroy(struct hwd_output *output) {
    assert(!output->enabled);
    assert(output_is_alive(output));

    wlr_log(WLR_DEBUG, "Destroying output '%s'", output->wlr_output->name);

    output->pending.dead = true;

    wl_signal_emit_mutable(&output->events.begin_destroy, output);

    output_set_dirty(output);
}

struct hwd_output *
output_from_wlr_output(struct wlr_output *output) {
    return output->data;
}

void
output_reconcile(struct hwd_output *output) {
    assert(output != NULL);

    struct hwd_workspace *workspace = root_get_active_workspace(root);
    if (workspace == NULL) {
        output->pending.fullscreen_window = NULL;
    }

    output->pending.fullscreen_window =
        workspace_get_fullscreen_window_for_output(workspace, output);

    output_set_dirty(output);
}

void
output_arrange(struct hwd_output *output) {
    if (config->reloading) {
        return;
    }
    struct wlr_box output_box;
    wlr_output_layout_get_box(root->output_layout, output->wlr_output, &output_box);
    output->lx = output_box.x;
    output->ly = output_box.y;
    output->width = output_box.width;
    output->height = output_box.height;

    if (output->pending.fullscreen_window) {
        struct hwd_window *fs = output->pending.fullscreen_window;
        fs->pending.x = output->lx;
        fs->pending.y = output->ly;
        fs->pending.width = output->width;
        fs->pending.height = output->height;
        window_arrange(fs);
    }
}

struct hwd_output *
output_get_in_direction(struct hwd_output *output, enum wlr_direction direction) {
    assert(direction);
    struct wlr_box output_box;
    wlr_output_layout_get_box(root->output_layout, output->wlr_output, &output_box);
    int lx = output_box.x + output_box.width / 2;
    int ly = output_box.y + output_box.height / 2;
    struct wlr_output *wlr_adjacent = wlr_output_layout_adjacent_output(
        root->output_layout, direction, output->wlr_output, lx, ly
    );
    if (!wlr_adjacent) {
        return NULL;
    }
    return output_from_wlr_output(wlr_adjacent);
}

void
output_get_box(struct hwd_output *output, struct wlr_box *box) {
    box->x = output->lx;
    box->y = output->ly;
    box->width = output->width;
    box->height = output->height;
}

void
output_get_usable_area(struct hwd_output *output, struct wlr_box *box) {
    box->x = output->usable_area.x;
    box->y = output->usable_area.y;
    box->width = output->usable_area.width;
    box->height = output->usable_area.height;
}

struct hwd_output *
output_by_name_or_id(const char *name_or_id) {
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hwd_output *hwd_output = root->outputs->items[i];
        struct wlr_output *wlr_output = hwd_output->wlr_output;

        char identifier[128];
        snprintf(
            identifier, 128, "%s %s %s", wlr_output->make, wlr_output->model, wlr_output->serial
        );

        if (strcasecmp(identifier, name_or_id) == 0 ||
            strcasecmp(hwd_output->wlr_output->name, name_or_id) == 0) {
            return hwd_output;
        }
    }
    return NULL;
}

static void
handle_destroy(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, destroy);

    output_begin_destroy(output);

    if (output->enabled) {
        output_disable(output);
    }

    wl_list_remove(&output->link);

    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->commit.link);
    wl_list_remove(&output->request_state.link);

    output->wlr_output->data = NULL;
    output->wlr_output = NULL;
}

static void
handle_commit(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, commit);
    struct wlr_output_event_commit *event = data;

    if (event->state->committed &
        (WLR_OUTPUT_STATE_TRANSFORM | WLR_OUTPUT_STATE_SCALE | WLR_OUTPUT_STATE_MODE)) {
        if (output->enabled) {
            arrange_layers(output);
            output_arrange(output);
        }
    }
}

static void
handle_request_state(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static unsigned int last_headless_num = 0;

void
handle_new_output(struct wl_listener *listener, void *data) {
    struct hwd_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    if (wlr_output_is_headless(wlr_output)) {
        char name[64];
        snprintf(name, sizeof(name), "HEADLESS-%u", ++last_headless_num);
        wlr_output_set_name(wlr_output, name);
    }

    wlr_log(
        WLR_DEBUG, "New output %p: %s (non-desktop: %d)", (void *)wlr_output, wlr_output->name,
        wlr_output->non_desktop
    );

    if (wlr_output->non_desktop) {
        wlr_log(WLR_DEBUG, "Not configuring non-desktop output");
        if (server->drm_lease_manager) {
            wlr_drm_lease_v1_manager_offer_output(server->drm_lease_manager, wlr_output);
        }
        return;
    }

    if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
        wlr_log(WLR_ERROR, "Failed to init output render");
        return;
    }

    struct wlr_scene_output *scene_output = wlr_scene_output_create(root->root_scene, wlr_output);
    assert(scene_output != NULL);

    hwd_scene_output_scheduler_create(scene_output);

    struct hwd_output *output = output_create(wlr_output);
    if (!output) {
        return;
    }

    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    output->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_output->events.commit, &output->commit);
    output->commit.notify = handle_commit;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->request_state.notify = handle_request_state;

    // BEGIN TODO this bit should ony be necessary if hwdout not running.
    wlr_output_enable(wlr_output, true);

    if (!wlr_output_commit(wlr_output)) {
        wlr_log(WLR_ERROR, "Failed to commit output %s", wlr_output->name);
        return;
    }

    struct wlr_output_layout_output *layout_output =
        wlr_output_layout_add(root->output_layout, wlr_output, 0, 0);
    wlr_scene_output_layout_add_output(root->scene_output_layout, layout_output, scene_output);

    struct wlr_box output_box;
    wlr_output_layout_get_box(root->output_layout, wlr_output, &output_box);
    output->lx = output_box.x;
    output->ly = output_box.y;
    output->width = output_box.width;
    output->height = output_box.height;

    output_enable(output);
    // END TODO
}

void
handle_output_layout_change(struct wl_listener *listener, void *data) {
    struct hwd_server *server = wl_container_of(listener, server, output_layout_change);

    // TODO.
}
