#ifndef HWD_INPUT_SEATOP_MOVE_TILING_H
#define HWD_INPUT_SEATOP_MOVE_TILING_H

#include <hayward/input/seat.h>
#include <hayward/tree/window.h>

void
seatop_begin_move_tiling_threshold(struct hwd_seat *seat, struct hwd_window *container);

void
seatop_begin_move_tiling(struct hwd_seat *seat, struct hwd_window *window);

#endif
