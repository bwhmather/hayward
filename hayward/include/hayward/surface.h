#ifndef HAYWARD_SURFACE_H
#define HAYWARD_SURFACE_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>

struct hayward_surface {
    struct wlr_surface *wlr_surface;

    struct wl_listener destroy;

    /**
     * This timer can be used for issuing delayed frame done callbacks (for
     * example, to improve presentation latency). Its handler is set to a
     * function that issues a frame done callback to this surface.
     */
    struct wl_event_source *frame_done_timer;
};

#endif
