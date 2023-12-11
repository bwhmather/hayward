#ifndef HWD_INPUT_SEATOP_RESIZE_FLOATING_H
#define HWD_INPUT_SEATOP_RESIZE_FLOATING_H

#include <wlr/util/edges.h>

#include <hayward/input/seat.h>
#include <hayward/tree/window.h>

void
seatop_begin_resize_floating(struct hwd_seat *seat, struct hwd_window *window, enum wlr_edges edge);

#endif
