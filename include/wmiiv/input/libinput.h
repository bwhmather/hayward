#ifndef _SWAY_INPUT_LIBINPUT_H
#define _SWAY_INPUT_LIBINPUT_H
#include "wmiiv/input/input-manager.h"

void wmiiv_input_configure_libinput_device(struct wmiiv_input_device *device);

void wmiiv_input_reset_libinput_device(struct wmiiv_input_device *device);

bool wmiiv_libinput_device_is_builtin(struct wmiiv_input_device *device);

#endif
