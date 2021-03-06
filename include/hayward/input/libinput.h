#ifndef _HAYWARD_INPUT_LIBINPUT_H
#define _HAYWARD_INPUT_LIBINPUT_H
#include "hayward/input/input-manager.h"

void hayward_input_configure_libinput_device(struct hayward_input_device *device);

void hayward_input_reset_libinput_device(struct hayward_input_device *device);

bool hayward_libinput_device_is_builtin(struct hayward_input_device *device);

#endif
