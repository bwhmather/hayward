#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/scheduler.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>

#include <hayward/profiler.h>
#include <hayward/server.h>

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
    .name = "hwd_buffer_timer", .destroy = handle_buffer_timer_destroy
};

static struct buffer_timer *
buffer_timer_assign(struct wlr_scene_buffer *buffer) {
    struct buffer_timer *timer = calloc(1, sizeof(struct buffer_timer));
    assert(timer != NULL);

    timer->frame_done_timer =
        wl_event_loop_add_timer(server.wl_event_loop, handle_buffer_timer, buffer);
    assert(buffer != NULL);

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
    struct hwd_scene_output_scheduler *scheduler_output;
};

static void
send_frame_done_iterator(struct wlr_scene_buffer *buffer, int x, int y, void *user_data) {
    struct send_frame_done_data *data = user_data;
    struct hwd_scene_output_scheduler *scheduler_output = data->scheduler_output;

    if (buffer->primary_output == NULL &&
        buffer->primary_output != scheduler_output->scene_output) {
        return;
    }

    int delay = data->msec_until_refresh - scheduler_output->max_render_time;
    // TODO factor in buffer max render time.

    struct buffer_timer *timer = NULL;

    if (delay > 0) {
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
    HWD_PROFILER_TRACE();

    struct hwd_scene_output_scheduler *scheduler_output = data;

    wlr_scene_output_commit(scheduler_output->scene_output, NULL);

    return 0;
}

static void
handle_output_frame(struct wl_listener *listener, void *user_data) {
    struct hwd_scene_output_scheduler *scheduler_output =
        wl_container_of(listener, scheduler_output, output_frame);
    if (!scheduler_output->scene_output->output->enabled) {
        return;
    }

    // Compute predicted milliseconds until the next refresh. It's used for
    // delaying both output rendering and surface frame callbacks.
    int msec_until_refresh = 0;

    if (scheduler_output->max_render_time != 0) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        const long NSEC_IN_SECONDS = 1000000000;
        struct timespec predicted_refresh = scheduler_output->last_presentation;
        predicted_refresh.tv_nsec += scheduler_output->refresh_nsec % NSEC_IN_SECONDS;
        predicted_refresh.tv_sec += scheduler_output->refresh_nsec / NSEC_IN_SECONDS;
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

    int delay = msec_until_refresh - scheduler_output->max_render_time;

    // If the delay is less than 1 millisecond (which is the least we can wait)
    // then just render right away.
    if (delay < 1) {
        output_repaint_timer_handler(scheduler_output);
    } else {
        scheduler_output->scene_output->output->frame_pending = true;
        wl_event_source_timer_update(scheduler_output->repaint_timer, delay);
    }

    // Send frame done to all visible surfaces
    struct send_frame_done_data data = {0};
    clock_gettime(CLOCK_MONOTONIC, &data.when);
    data.msec_until_refresh = msec_until_refresh;
    data.scheduler_output = scheduler_output;
    wlr_scene_output_for_each_buffer(
        scheduler_output->scene_output, send_frame_done_iterator, &data
    );
}

static void
handle_scene_output_destroy(struct wl_listener *listener, void *data) {
    struct hwd_scene_output_scheduler *scheduler_output =
        wl_container_of(listener, scheduler_output, scene_output_destroy);

    wl_list_remove(&scheduler_output->scene_output_destroy.link);
    wl_list_remove(&scheduler_output->output_present.link);
    wl_list_remove(&scheduler_output->output_frame.link);

    scheduler_output->scene_output = NULL;

    wl_event_source_remove(scheduler_output->repaint_timer);
    scheduler_output->repaint_timer = NULL;

    free(scheduler_output);
}

static void
handle_output_present(struct wl_listener *listener, void *data) {
    struct hwd_scene_output_scheduler *scheduler_output =
        wl_container_of(listener, scheduler_output, output_present);
    struct wlr_output_event_present *output_event = data;

    if (!output_event->presented) {
        return;
    }

    scheduler_output->last_presentation = output_event->when;
    scheduler_output->refresh_nsec = output_event->refresh;
}

struct hwd_scene_output_scheduler *
hwd_scene_output_scheduler_create(struct wlr_scene_output *scene_output) {
    struct wlr_output *wlr_output = scene_output->output;

    struct hwd_scene_output_scheduler *scheduler_output =
        calloc(1, sizeof(struct hwd_scene_output_scheduler));
    assert(scheduler_output != NULL);

    scheduler_output->scene_output = scene_output;

    scheduler_output->scene_output_destroy.notify = handle_scene_output_destroy;
    wl_signal_add(&scene_output->events.destroy, &scheduler_output->scene_output_destroy);
    scheduler_output->output_present.notify = handle_output_present;
    wl_signal_add(&wlr_output->events.present, &scheduler_output->output_present);
    scheduler_output->output_frame.notify = handle_output_frame;
    wl_signal_add(&wlr_output->events.frame, &scheduler_output->output_frame);

    struct wl_event_loop *event_loop = wlr_output->event_loop;
    scheduler_output->repaint_timer =
        wl_event_loop_add_timer(event_loop, output_repaint_timer_handler, scheduler_output);

    return scheduler_output;
}
