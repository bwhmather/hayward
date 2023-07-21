#ifndef HWD_INPUT_LIBINPUT_H
#define HWD_INPUT_LIBINPUT_H

#include <stdbool.h>

#include <hayward/input/input_manager.h>

void
hwd_input_configure_libinput_device(struct hwd_input_device *device);

void
hwd_input_reset_libinput_device(struct hwd_input_device *device);

bool
hwd_libinput_device_is_builtin(struct hwd_input_device *device);

#endif
