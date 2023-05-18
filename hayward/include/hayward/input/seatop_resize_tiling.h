#ifndef _HAYWARD_INPUT_SEATOP_RESIZE_TILING_H
#define _HAYWARD_INPUT_SEATOP_RESIZE_TILING_H

#include <wlr/util/edges.h>

#include <hayward/input/seat.h>
#include <hayward/tree/window.h>

void
seatop_begin_resize_tiling(
    struct hayward_seat *seat, struct hayward_window *window,
    enum wlr_edges edge
);

#endif
