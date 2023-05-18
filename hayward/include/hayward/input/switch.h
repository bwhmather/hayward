#ifndef HAYWARD_INPUT_SWITCH_H
#define HAYWARD_INPUT_SWITCH_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_switch.h>

#include <hayward/input/seat.h>

struct hayward_switch {
    struct hayward_seat_device *seat_device;
    struct wlr_switch *wlr;

    enum wlr_switch_state state;
    enum wlr_switch_type type;

    struct wl_listener switch_toggle;
};

struct hayward_switch *
hayward_switch_create(
    struct hayward_seat *seat, struct hayward_seat_device *device
);

void
hayward_switch_configure(struct hayward_switch *hayward_switch);

void
hayward_switch_destroy(struct hayward_switch *hayward_switch);

void
hayward_switch_retrigger_bindings_for_all(void);

#endif
