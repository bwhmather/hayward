#ifndef HWD_INPUT_SEATOP_DOWN_H
#define HWD_INPUT_SEATOP_DOWN_H

#include <stdint.h>
#include <wlr/types/wlr_compositor.h>

#include <hayward/input/seat.h>
#include <hayward/tree/window.h>

void
seatop_begin_down(
    struct hwd_seat *seat, struct hwd_window *window, uint32_t time_msec, double sx, double sy
);

void
seatop_begin_down_on_surface(
    struct hwd_seat *seat, struct wlr_surface *surface, uint32_t time_msec, double sx, double sy
);

#endif
