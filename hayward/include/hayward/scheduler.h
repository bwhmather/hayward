#ifndef HWD_SCHEDULER_H
#define HWD_SCHEDULER_H

#include <config.h>

#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>

struct hwd_scene_output_scheduler {
    struct wlr_scene_output *scene_output;

    struct timespec last_frame;

    struct wl_listener scene_output_destroy;
    struct wl_listener output_present;
    struct wl_listener output_frame;

    struct timespec last_presentation;
    uint32_t refresh_nsec;
    int max_render_time; // In milliseconds
    struct wl_event_source *repaint_timer;
};

struct hwd_scene_output_scheduler *
hwd_scene_output_scheduler_create(struct wlr_scene_output *scene_output);

#endif
