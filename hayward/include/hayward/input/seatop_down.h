#ifndef HAYWARD_INPUT_SEATOP_DOWN_H
#define HAYWARD_INPUT_SEATOP_DOWN_H

#include <stdint.h>
#include <wlr/types/wlr_compositor.h>

#include <hayward/input/seat.h>
#include <hayward/tree/window.h>

void
seatop_begin_down(
    struct hayward_seat *seat, struct hayward_window *window,
    uint32_t time_msec, double sx, double sy
);

void
seatop_begin_down_on_surface(
    struct hayward_seat *seat, struct wlr_surface *surface, uint32_t time_msec,
    double sx, double sy
);

#endif
