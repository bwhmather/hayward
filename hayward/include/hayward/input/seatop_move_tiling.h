#ifndef _HAYWARD_INPUT_SEATOP_MOVE_TILING_H
#define _HAYWARD_INPUT_SEATOP_MOVE_TILING_H

#include <hayward/input/seat.h>
#include <hayward/tree/window.h>

void
seatop_begin_move_tiling_threshold(
    struct hayward_seat *seat, struct hayward_window *container
);

void
seatop_begin_move_tiling(
    struct hayward_seat *seat, struct hayward_window *window
);

#endif
