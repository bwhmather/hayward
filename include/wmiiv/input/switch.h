#ifndef _SWAY_INPUT_SWITCH_H
#define _SWAY_INPUT_SWITCH_H

#include "wmiiv/input/seat.h"

struct wmiiv_switch {
	struct wmiiv_seat_device *seat_device;
	enum wlr_switch_state state;
	enum wlr_switch_type type;

	struct wl_listener switch_toggle;
};

struct wmiiv_switch *wmiiv_switch_create(struct wmiiv_seat *seat,
		struct wmiiv_seat_device *device);

void wmiiv_switch_configure(struct wmiiv_switch *wmiiv_switch);

void wmiiv_switch_destroy(struct wmiiv_switch *wmiiv_switch);

void wmiiv_switch_retrigger_bindings_for_all(void);

#endif
