#ifndef HWD_INPUT_SWITCH_H
#define HWD_INPUT_SWITCH_H

#include <wayland-server-core.h>

#include <wlr/types/wlr_switch.h>

#include <hayward/input/seat.h>

struct hwd_switch {
    struct hwd_seat_device *seat_device;
    struct wlr_switch *wlr;

    enum wlr_switch_state state;
    enum wlr_switch_type type;

    struct wl_listener switch_toggle;
};

struct hwd_switch *
hwd_switch_create(struct hwd_seat *seat, struct hwd_seat_device *device);

void
hwd_switch_configure(struct hwd_switch *hwd_switch);

void
hwd_switch_destroy(struct hwd_switch *hwd_switch);

void
hwd_switch_retrigger_bindings_for_all(void);

#endif
