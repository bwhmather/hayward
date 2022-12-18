#ifndef _HAYWARD_INPUT_SWITCH_H
#define _HAYWARD_INPUT_SWITCH_H

#include "hayward/input/seat.h"

struct hayward_switch {
    struct hayward_seat_device *seat_device;
    enum wlr_switch_state state;
    enum wlr_switch_type type;

    struct wl_listener switch_toggle;
};

struct hayward_switch *hayward_switch_create(
    struct hayward_seat *seat, struct hayward_seat_device *device
);

void hayward_switch_configure(struct hayward_switch *hayward_switch);

void hayward_switch_destroy(struct hayward_switch *hayward_switch);

void hayward_switch_retrigger_bindings_for_all(void);

#endif
