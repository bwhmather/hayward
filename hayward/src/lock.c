#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_session_lock_v1.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/root.h>

#include <config.h>

struct hayward_session_lock_surface {
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct hayward_output *output;
    struct wlr_surface *surface;
    struct wl_listener map;
    struct wl_listener destroy;
    struct wl_listener surface_commit;
    struct wl_listener output_mode;
    struct wl_listener output_commit;
};

static void
handle_surface_map(struct wl_listener *listener, void *data) {
    struct hayward_session_lock_surface *surf =
        wl_container_of(listener, surf, map);
    hayward_force_focus(surf->surface);
    output_damage_whole(surf->output);
}

static void
handle_surface_commit(struct wl_listener *listener, void *data) {
    struct hayward_session_lock_surface *surf =
        wl_container_of(listener, surf, surface_commit);
    output_damage_surface(surf->output, 0, 0, surf->surface, false);
}

static void
handle_output_mode(struct wl_listener *listener, void *data) {
    struct hayward_session_lock_surface *surf =
        wl_container_of(listener, surf, output_mode);
    wlr_session_lock_surface_v1_configure(
        surf->lock_surface, surf->output->width, surf->output->height
    );
}

static void
handle_output_commit(struct wl_listener *listener, void *data) {
    struct wlr_output_event_commit *event = data;
    struct hayward_session_lock_surface *surf =
        wl_container_of(listener, surf, output_commit);
    if (event->committed &
        (WLR_OUTPUT_STATE_MODE | WLR_OUTPUT_STATE_SCALE |
         WLR_OUTPUT_STATE_TRANSFORM)) {
        wlr_session_lock_surface_v1_configure(
            surf->lock_surface, surf->output->width, surf->output->height
        );
    }
}

static void
handle_surface_destroy(struct wl_listener *listener, void *data) {
    struct hayward_session_lock_surface *surf =
        wl_container_of(listener, surf, destroy);
    wl_list_remove(&surf->map.link);
    wl_list_remove(&surf->destroy.link);
    wl_list_remove(&surf->surface_commit.link);
    wl_list_remove(&surf->output_mode.link);
    wl_list_remove(&surf->output_commit.link);
    output_damage_whole(surf->output);
    free(surf);
}

static void
handle_new_surface(struct wl_listener *listener, void *data) {
    struct wlr_session_lock_surface_v1 *lock_surface = data;
    struct hayward_session_lock_surface *surf = calloc(1, sizeof(*surf));
    if (surf == NULL) {
        return;
    }

    hayward_log(HAYWARD_DEBUG, "new lock layer surface");

    struct hayward_output *output = lock_surface->output->data;
    wlr_session_lock_surface_v1_configure(
        lock_surface, output->width, output->height
    );

    surf->lock_surface = lock_surface;
    surf->surface = lock_surface->surface;
    surf->output = output;
    surf->map.notify = handle_surface_map;
    wl_signal_add(&lock_surface->events.map, &surf->map);
    surf->destroy.notify = handle_surface_destroy;
    wl_signal_add(&lock_surface->events.destroy, &surf->destroy);
    surf->surface_commit.notify = handle_surface_commit;
    wl_signal_add(&surf->surface->events.commit, &surf->surface_commit);
    surf->output_mode.notify = handle_output_mode;
    wl_signal_add(&output->wlr_output->events.mode, &surf->output_mode);
    surf->output_commit.notify = handle_output_commit;
    wl_signal_add(&output->wlr_output->events.commit, &surf->output_commit);
}

static void
handle_unlock(struct wl_listener *listener, void *data) {
    hayward_log(HAYWARD_DEBUG, "session unlocked");
    server.session_lock.locked = false;
    server.session_lock.lock = NULL;

    wl_list_remove(&server.session_lock.lock_new_surface.link);
    wl_list_remove(&server.session_lock.lock_unlock.link);
    wl_list_remove(&server.session_lock.lock_destroy.link);

    // redraw everything
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hayward_output *output = root->outputs->items[i];
        output_damage_whole(output);
    }
}

static void
handle_abandon(struct wl_listener *listener, void *data) {
    hayward_log(HAYWARD_INFO, "session lock abandoned");
    server.session_lock.lock = NULL;

    wl_list_remove(&server.session_lock.lock_new_surface.link);
    wl_list_remove(&server.session_lock.lock_unlock.link);
    wl_list_remove(&server.session_lock.lock_destroy.link);

    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seat->exclusive_client = NULL;
    }

    // redraw everything
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hayward_output *output = root->outputs->items[i];
        output_damage_whole(output);
    }
}

static void
handle_session_lock(struct wl_listener *listener, void *data) {
    struct wlr_session_lock_v1 *lock = data;
    struct wl_client *client = wl_resource_get_client(lock->resource);

    if (server.session_lock.lock) {
        wlr_session_lock_v1_destroy(lock);
        return;
    }

    hayward_log(HAYWARD_DEBUG, "session locked");
    server.session_lock.locked = true;
    server.session_lock.lock = lock;

    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seat_set_exclusive_client(seat, client);
    }

    wl_signal_add(
        &lock->events.new_surface, &server.session_lock.lock_new_surface
    );
    wl_signal_add(&lock->events.unlock, &server.session_lock.lock_unlock);
    wl_signal_add(&lock->events.destroy, &server.session_lock.lock_destroy);

    wlr_session_lock_v1_send_locked(lock);

    // redraw everything
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hayward_output *output = root->outputs->items[i];
        output_damage_whole(output);
    }
}

static void
handle_session_lock_destroy(struct wl_listener *listener, void *data) {
    assert(server.session_lock.lock == NULL);
    wl_list_remove(&server.session_lock.new_lock.link);
    wl_list_remove(&server.session_lock.manager_destroy.link);
}

void
hayward_session_lock_init(void) {
    server.session_lock.manager =
        wlr_session_lock_manager_v1_create(server.wl_display);

    server.session_lock.lock_new_surface.notify = handle_new_surface;
    server.session_lock.lock_unlock.notify = handle_unlock;
    server.session_lock.lock_destroy.notify = handle_abandon;
    server.session_lock.new_lock.notify = handle_session_lock;
    server.session_lock.manager_destroy.notify = handle_session_lock_destroy;
    wl_signal_add(
        &server.session_lock.manager->events.new_lock,
        &server.session_lock.new_lock
    );
    wl_signal_add(
        &server.session_lock.manager->events.destroy,
        &server.session_lock.manager_destroy
    );
}
