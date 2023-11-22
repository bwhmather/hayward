#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include "hayward/output.h"

#include <config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/backend/headless.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/desktop/layer_shell.h>
#include <hayward/globals/root.h>
#include <hayward/input/input_manager.h>
#include <hayward/ipc_server.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/view.h>
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

    hwd_assert(output->current.dead, "After apply called on live output");
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
    hwd_assert(output != NULL, "Expected output");
    return !output->pending.dead;
}

static void
output_set_dirty(struct hwd_output *output) {
    hwd_assert(output != NULL, "Expected output");

    struct hwd_transaction_manager *transaction_manager = root_get_transaction_manager(root);

    if (output->dirty) {
        return;
    }

    output->dirty = true;
    wl_signal_add(&transaction_manager->events.commit, &output->transaction_commit);
    hwd_transaction_manager_ensure_queued(transaction_manager);
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

                ipc_event_window(window, "move");
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

            ipc_event_window(window, "move");
        }

        workspace_arrange(workspace);
    }
}

static void
output_destroy(struct hwd_output *output) {
    hwd_assert(output->current.dead, "Tried to free output which wasn't marked as destroying");
    hwd_assert(output->wlr_output == NULL, "Tried to free output which still had a wlr_output");
    hwd_assert(!output->dirty, "Tried to free output which is queued for the next transaction");
    wl_event_source_remove(output->repaint_timer);

    output_destroy_scene(output);

    free(output);
}

static void
output_disable(struct hwd_output *output) {
    hwd_assert(output->enabled, "Expected an enabled output");

    int index = list_find(root->outputs, output);
    hwd_assert(index >= 0, "Output not found in root node");

    hwd_log(HWD_DEBUG, "Disabling output '%s'", output->wlr_output->name);
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
    hwd_assert(!output->enabled, "Expected a disabled output");
    hwd_assert(output_is_alive(output), "Expected live output");

    hwd_log(HWD_DEBUG, "Destroying output '%s'", output->wlr_output->name);

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
    hwd_assert(output != NULL, "Expected output");

    struct hwd_workspace *workspace = root_get_active_workspace(root);
    if (workspace == NULL) {
        output->pending.fullscreen_window = NULL;
    }

    output->pending.fullscreen_window =
        workspace_get_fullscreen_window_for_output(workspace, output);

    output_set_dirty(output);
}

struct hwd_output *
output_get_in_direction(struct hwd_output *output, enum wlr_direction direction) {
    hwd_assert(direction, "got invalid direction: %d", direction);
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

struct buffer_timer {
    struct wlr_addon addon;
    struct wl_event_source *frame_done_timer;
};

static int
handle_buffer_timer(void *data) {
    struct wlr_scene_buffer *buffer = data;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_buffer_send_frame_done(buffer, &now);
    return 0;
}

static void
handle_buffer_timer_destroy(struct wlr_addon *addon) {
    struct buffer_timer *timer = wl_container_of(addon, timer, addon);
    wl_event_source_remove(timer->frame_done_timer);
    free(timer);
}

static const struct wlr_addon_interface buffer_timer_interface = {
    .name = "hwd_buffer_timer", .destroy = handle_buffer_timer_destroy};

static struct buffer_timer *
buffer_timer_assign(struct wlr_scene_buffer *buffer) {
    struct buffer_timer *timer = calloc(1, sizeof(struct buffer_timer));
    hwd_assert(timer != NULL, "Allocation failed");

    timer->frame_done_timer =
        wl_event_loop_add_timer(server.wl_event_loop, handle_buffer_timer, buffer);
    hwd_assert(buffer != NULL, "Could not create timer");

    wlr_addon_init(
        &timer->addon, &buffer->node.addons, &buffer_timer_interface, &buffer_timer_interface
    );

    return timer;
}

static struct buffer_timer *
buffer_timer_try_get(struct wlr_scene_buffer *buffer) {
    struct wlr_addon *addon =
        wlr_addon_find(&buffer->node.addons, &buffer_timer_interface, &buffer_timer_interface);
    if (addon == NULL) {
        return NULL;
    }

    struct buffer_timer *timer;
    timer = wl_container_of(addon, timer, addon);

    return timer;
}

struct send_frame_done_data {
    struct timespec when;
    int msec_until_refresh;
    struct hwd_output *output;
};

static void
send_frame_done_iterator(struct wlr_scene_buffer *buffer, int x, int y, void *user_data) {
    struct send_frame_done_data *data = user_data;
    struct hwd_output *output = data->output;
    int view_max_render_time = 0;

    if (buffer->primary_output == NULL && buffer->primary_output != data->output->scene_output) {
        return;
    }

    struct wlr_scene_node *current = &buffer->node;

    while (true) {
        struct hwd_window *window = window_for_scene_node(current);
        if (window != NULL) {
            view_max_render_time = window->view->max_render_time;
            break;
        }

        if (!current->parent) {
            break;
        }

        current = &current->parent->node;
    }

    int delay = data->msec_until_refresh - output->max_render_time - view_max_render_time;

    struct buffer_timer *timer = NULL;

    if (output->max_render_time != 0 && view_max_render_time != 0 && delay > 0) {
        timer = buffer_timer_try_get(buffer);

        if (!timer) {
            timer = buffer_timer_assign(buffer);
        }
    }

    if (timer) {
        wl_event_source_timer_update(timer->frame_done_timer, delay);
    } else {
        wlr_scene_buffer_send_frame_done(buffer, &data->when);
    }
}

static int
output_repaint_timer_handler(void *data) {
    struct hwd_output *output = data;
    if (output->wlr_output == NULL) {
        return 0;
    }

    if (output->enabled) {
        wlr_scene_output_commit(output->scene_output, NULL);
    }

    return 0;
}

static void
handle_frame(struct wl_listener *listener, void *user_data) {
    struct hwd_output *output = wl_container_of(listener, output, frame);
    if (!output->enabled || !output->wlr_output->enabled) {
        return;
    }

    // Compute predicted milliseconds until the next refresh. It's used for
    // delaying both output rendering and surface frame callbacks.
    int msec_until_refresh = 0;

    if (output->max_render_time != 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        const long NSEC_IN_SECONDS = 1000000000;
        struct timespec predicted_refresh = output->last_presentation;
        predicted_refresh.tv_nsec += output->refresh_nsec % NSEC_IN_SECONDS;
        predicted_refresh.tv_sec += output->refresh_nsec / NSEC_IN_SECONDS;
        if (predicted_refresh.tv_nsec >= NSEC_IN_SECONDS) {
            predicted_refresh.tv_sec += 1;
            predicted_refresh.tv_nsec -= NSEC_IN_SECONDS;
        }

        // If the predicted refresh time is before the current time then
        // there's no point in delaying.
        //
        // We only check tv_sec because if the predicted refresh time is less
        // than a second before the current time, then msec_until_refresh will
        // end up slightly below zero, which will effectively disable the delay
        // without potential disastrous negative overflows that could occur if
        // tv_sec was not checked.
        if (predicted_refresh.tv_sec >= now.tv_sec) {
            long nsec_until_refresh = (predicted_refresh.tv_sec - now.tv_sec) * NSEC_IN_SECONDS +
                (predicted_refresh.tv_nsec - now.tv_nsec);

            // We want msec_until_refresh to be conservative, that is, floored.
            // If we have 7.9 msec until refresh, we better compute the delay
            // as if we had only 7 msec, so that we don't accidentally delay
            // more than necessary and miss a frame.
            msec_until_refresh = nsec_until_refresh / 1000000;
        }
    }

    int delay = msec_until_refresh - output->max_render_time;

    // If the delay is less than 1 millisecond (which is the least we can wait)
    // then just render right away.
    if (delay < 1) {
        output_repaint_timer_handler(output);
    } else {
        output->wlr_output->frame_pending = true;
        wl_event_source_timer_update(output->repaint_timer, delay);
    }

    // Send frame done to all visible surfaces
    struct send_frame_done_data data = {0};
    clock_gettime(CLOCK_MONOTONIC, &data.when);
    data.msec_until_refresh = msec_until_refresh;
    data.output = output;
    wlr_scene_output_for_each_buffer(output->scene_output, send_frame_done_iterator, &data);
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
    wl_list_remove(&output->present.link);
    wl_list_remove(&output->request_state.link);

    wlr_scene_output_destroy(output->scene_output);
    output->scene_output = NULL;
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
handle_present(struct wl_listener *listener, void *data) {
    struct hwd_output *output = wl_container_of(listener, output, present);
    struct wlr_output_event_present *output_event = data;

    if (!output->enabled || !output_event->presented) {
        return;
    }

    output->last_presentation = *output_event->when;
    output->refresh_nsec = output_event->refresh;
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

    hwd_log(
        HWD_DEBUG, "New output %p: %s (non-desktop: %d)", (void *)wlr_output, wlr_output->name,
        wlr_output->non_desktop
    );

    if (wlr_output->non_desktop) {
        hwd_log(HWD_DEBUG, "Not configuring non-desktop output");
        if (server->drm_lease_manager) {
            wlr_drm_lease_v1_manager_offer_output(server->drm_lease_manager, wlr_output);
        }
        return;
    }

    if (!wlr_output_init_render(wlr_output, server->allocator, server->renderer)) {
        hwd_log(HWD_ERROR, "Failed to init output render");
        return;
    }

    struct wlr_scene_output *scene_output = wlr_scene_output_create(root->root_scene, wlr_output);
    hwd_assert(scene_output != NULL, "Allocation failed");

    struct hwd_output *output = output_create(wlr_output);
    if (!output) {
        return;
    }
    output->server = server;
    output->scene_output = scene_output;

    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    output->destroy.notify = handle_destroy;
    wl_signal_add(&wlr_output->events.commit, &output->commit);
    output->commit.notify = handle_commit;
    wl_signal_add(&wlr_output->events.present, &output->present);
    output->present.notify = handle_present;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    output->frame.notify = handle_frame;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);
    output->request_state.notify = handle_request_state;

    output->repaint_timer =
        wl_event_loop_add_timer(server->wl_event_loop, output_repaint_timer_handler, output);

    // BEGIN TODO this bit should ony be necessary if hwdout not running.
    wlr_output_enable(wlr_output, true);

    if (!wlr_output_commit(wlr_output)) {
        hwd_log(HWD_ERROR, "Failed to commit output %s", wlr_output->name);
        return;
    }

    wlr_output_layout_add(root->output_layout, wlr_output, 0, 0);

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
