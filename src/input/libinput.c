#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/input/libinput.h"

#include <float.h>
#include <inttypes.h>
#include <libinput.h>
#include <libudev.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <wlr/backend/libinput.h>
#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/input/input_manager.h>
#include <hayward/tree/output.h>

static void
log_status(enum libinput_config_status status) {
    if (status != LIBINPUT_CONFIG_STATUS_SUCCESS) {
        wlr_log(
            WLR_ERROR, "Failed to apply libinput config: %s", libinput_config_status_to_str(status)
        );
    }
}

static bool
set_send_events(struct libinput_device *device, uint32_t mode) {
    if (libinput_device_config_send_events_get_mode(device) == mode) {
        return false;
    }
    wlr_log(WLR_DEBUG, "send_events_set_mode(%i)", mode);
    log_status(libinput_device_config_send_events_set_mode(device, mode));
    return true;
}

static bool
set_tap(struct libinput_device *device, enum libinput_config_tap_state tap) {
    if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
        libinput_device_config_tap_get_enabled(device) == tap) {
        return false;
    }
    wlr_log(WLR_DEBUG, "tap_set_enabled(%d)", tap);
    log_status(libinput_device_config_tap_set_enabled(device, tap));
    return true;
}

static bool
set_tap_button_map(struct libinput_device *device, enum libinput_config_tap_button_map map) {
    if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
        libinput_device_config_tap_get_button_map(device) == map) {
        return false;
    }
    wlr_log(WLR_DEBUG, "tap_set_button_map(%d)", map);
    log_status(libinput_device_config_tap_set_button_map(device, map));
    return true;
}

static bool
set_tap_drag(struct libinput_device *device, enum libinput_config_drag_state drag) {
    if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
        libinput_device_config_tap_get_drag_enabled(device) == drag) {
        return false;
    }
    wlr_log(WLR_DEBUG, "tap_set_drag_enabled(%d)", drag);
    log_status(libinput_device_config_tap_set_drag_enabled(device, drag));
    return true;
}

static bool
set_tap_drag_lock(struct libinput_device *device, enum libinput_config_drag_lock_state lock) {
    if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
        libinput_device_config_tap_get_drag_lock_enabled(device) == lock) {
        return false;
    }
    wlr_log(WLR_DEBUG, "tap_set_drag_lock_enabled(%d)", lock);
    log_status(libinput_device_config_tap_set_drag_lock_enabled(device, lock));
    return true;
}

static bool
set_accel_speed(struct libinput_device *device, double speed) {
    if (!libinput_device_config_accel_is_available(device) ||
        libinput_device_config_accel_get_speed(device) == speed) {
        return false;
    }
    wlr_log(WLR_DEBUG, "accel_set_speed(%f)", speed);
    log_status(libinput_device_config_accel_set_speed(device, speed));
    return true;
}

static bool
set_accel_profile(struct libinput_device *device, enum libinput_config_accel_profile profile) {
    if (!libinput_device_config_accel_is_available(device) ||
        libinput_device_config_accel_get_profile(device) == profile) {
        return false;
    }
    wlr_log(WLR_DEBUG, "accel_set_profile(%d)", profile);
    log_status(libinput_device_config_accel_set_profile(device, profile));
    return true;
}

static bool
set_natural_scroll(struct libinput_device *d, bool n) {
    if (!libinput_device_config_scroll_has_natural_scroll(d) ||
        libinput_device_config_scroll_get_natural_scroll_enabled(d) == n) {
        return false;
    }
    wlr_log(WLR_DEBUG, "scroll_set_natural_scroll(%d)", n);
    log_status(libinput_device_config_scroll_set_natural_scroll_enabled(d, n));
    return true;
}

static bool
set_left_handed(struct libinput_device *device, bool left) {
    if (!libinput_device_config_left_handed_is_available(device) ||
        libinput_device_config_left_handed_get(device) == left) {
        return false;
    }
    wlr_log(WLR_DEBUG, "left_handed_set(%d)", left);
    log_status(libinput_device_config_left_handed_set(device, left));
    return true;
}

static bool
set_click_method(struct libinput_device *device, enum libinput_config_click_method method) {
    uint32_t click = libinput_device_config_click_get_methods(device);
    if ((click & ~LIBINPUT_CONFIG_CLICK_METHOD_NONE) == 0 ||
        libinput_device_config_click_get_method(device) == method) {
        return false;
    }
    wlr_log(WLR_DEBUG, "click_set_method(%d)", method);
    log_status(libinput_device_config_click_set_method(device, method));
    return true;
}

static bool
set_middle_emulation(struct libinput_device *dev, enum libinput_config_middle_emulation_state mid) {
    if (!libinput_device_config_middle_emulation_is_available(dev) ||
        libinput_device_config_middle_emulation_get_enabled(dev) == mid) {
        return false;
    }
    wlr_log(WLR_DEBUG, "middle_emulation_set_enabled(%d)", mid);
    log_status(libinput_device_config_middle_emulation_set_enabled(dev, mid));
    return true;
}

static bool
set_scroll_method(struct libinput_device *device, enum libinput_config_scroll_method method) {
    uint32_t scroll = libinput_device_config_scroll_get_methods(device);
    if ((scroll & ~LIBINPUT_CONFIG_SCROLL_NO_SCROLL) == 0 ||
        libinput_device_config_scroll_get_method(device) == method) {
        return false;
    }
    wlr_log(WLR_DEBUG, "scroll_set_method(%d)", method);
    log_status(libinput_device_config_scroll_set_method(device, method));
    return true;
}

static bool
set_scroll_button(struct libinput_device *dev, uint32_t button) {
    uint32_t scroll = libinput_device_config_scroll_get_methods(dev);
    if ((scroll & ~LIBINPUT_CONFIG_SCROLL_NO_SCROLL) == 0 ||
        libinput_device_config_scroll_get_button(dev) == button) {
        return false;
    }
    wlr_log(WLR_DEBUG, "scroll_set_button(%" PRIu32 ")", button);
    log_status(libinput_device_config_scroll_set_button(dev, button));
    return true;
}

static bool
set_dwt(struct libinput_device *device, bool dwt) {
    if (!libinput_device_config_dwt_is_available(device) ||
        libinput_device_config_dwt_get_enabled(device) == dwt) {
        return false;
    }
    wlr_log(WLR_DEBUG, "dwt_set_enabled(%d)", dwt);
    log_status(libinput_device_config_dwt_set_enabled(device, dwt));
    return true;
}

static bool
set_calibration_matrix(struct libinput_device *dev, float mat[6]) {
    if (!libinput_device_config_calibration_has_matrix(dev)) {
        return false;
    }
    bool changed = false;
    float current[6];
    libinput_device_config_calibration_get_matrix(dev, current);
    for (int i = 0; i < 6; i++) {
        if (current[i] != mat[i]) {
            changed = true;
            break;
        }
    }
    if (changed) {
        wlr_log(
            WLR_DEBUG, "calibration_set_matrix(%f, %f, %f, %f, %f, %f)", mat[0], mat[1], mat[2],
            mat[3], mat[4], mat[5]
        );
        log_status(libinput_device_config_calibration_set_matrix(dev, mat));
    }
    return changed;
}

void
hwd_input_configure_libinput_device(struct hwd_input_device *input_device) {
    struct input_config *ic = input_device_get_config(input_device);
    if (!ic || !wlr_input_device_is_libinput(input_device->wlr_device)) {
        return;
    }

    struct libinput_device *device = wlr_libinput_get_device_handle(input_device->wlr_device);
    wlr_log(
        WLR_DEBUG, "hwd_input_configure_libinput_device('%s' on '%s')", ic->identifier,
        input_device->identifier
    );

    bool changed = false;
    if (ic->mapped_to_output && !output_by_name_or_id(ic->mapped_to_output)) {
        wlr_log(
            WLR_DEBUG, "%s '%s' is mapped to offline output '%s'; disabling input", ic->input_type,
            ic->identifier, ic->mapped_to_output
        );
        set_send_events(device, LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
    } else if (ic->send_events != INT_MIN) {
        set_send_events(device, ic->send_events);
    } else {
        // Have to reset to the default mode here, otherwise if ic->send_events
        // is unset and a mapped output just came online after being disabled,
        // we'd remain stuck sending no events.
        changed |=
            set_send_events(device, libinput_device_config_send_events_get_default_mode(device));
    }

    if (ic->tap != INT_MIN) {
        set_tap(device, ic->tap);
    }
    if (ic->tap_button_map != INT_MIN) {
        set_tap_button_map(device, ic->tap_button_map);
    }
    if (ic->drag != INT_MIN) {
        set_tap_drag(device, ic->drag);
    }
    if (ic->drag_lock != INT_MIN) {
        set_tap_drag_lock(device, ic->drag_lock);
    }
    if (ic->pointer_accel != FLT_MIN) {
        set_accel_speed(device, ic->pointer_accel);
    }
    if (ic->accel_profile != INT_MIN) {
        set_accel_profile(device, ic->accel_profile);
    }
    if (ic->natural_scroll != INT_MIN) {
        set_natural_scroll(device, ic->natural_scroll);
    }
    if (ic->left_handed != INT_MIN) {
        set_left_handed(device, ic->left_handed);
    }
    if (ic->click_method != INT_MIN) {
        set_click_method(device, ic->click_method);
    }
    if (ic->middle_emulation != INT_MIN) {
        set_middle_emulation(device, ic->middle_emulation);
    }
    if (ic->scroll_method != INT_MIN) {
        set_scroll_method(device, ic->scroll_method);
    }
    if (ic->scroll_button != INT_MIN) {
        set_scroll_button(device, ic->scroll_button);
    }
    if (ic->dwt != INT_MIN) {
        set_dwt(device, ic->dwt);
    }
    if (ic->calibration_matrix.configured) {
        set_calibration_matrix(device, ic->calibration_matrix.matrix);
    }
}

void
hwd_input_reset_libinput_device(struct hwd_input_device *input_device) {
    if (!wlr_input_device_is_libinput(input_device->wlr_device)) {
        return;
    }

    struct libinput_device *device = wlr_libinput_get_device_handle(input_device->wlr_device);
    wlr_log(WLR_DEBUG, "hwd_input_reset_libinput_device(%s)", input_device->identifier);
    bool changed = false;

    set_send_events(device, libinput_device_config_send_events_get_default_mode(device));
    set_tap(device, libinput_device_config_tap_get_default_enabled(device));
    changed |=
        set_tap_button_map(device, libinput_device_config_tap_get_default_button_map(device));
    set_tap_drag(device, libinput_device_config_tap_get_default_drag_enabled(device));
    changed |=
        set_tap_drag_lock(device, libinput_device_config_tap_get_default_drag_lock_enabled(device));
    set_accel_speed(device, libinput_device_config_accel_get_default_speed(device));
    set_accel_profile(device, libinput_device_config_accel_get_default_profile(device));
    set_natural_scroll(
        device, libinput_device_config_scroll_get_default_natural_scroll_enabled(device)
    );
    set_left_handed(device, libinput_device_config_left_handed_get_default(device));
    set_click_method(device, libinput_device_config_click_get_default_method(device));
    set_middle_emulation(
        device, libinput_device_config_middle_emulation_get_default_enabled(device)
    );
    set_scroll_method(device, libinput_device_config_scroll_get_default_method(device));
    set_scroll_button(device, libinput_device_config_scroll_get_default_button(device));
    set_dwt(device, libinput_device_config_dwt_get_default_enabled(device));

    float matrix[6];
    libinput_device_config_calibration_get_default_matrix(device, matrix);
    set_calibration_matrix(device, matrix);
}

bool
hwd_libinput_device_is_builtin(struct hwd_input_device *hwd_device) {
    if (!wlr_input_device_is_libinput(hwd_device->wlr_device)) {
        return false;
    }

    struct libinput_device *device = wlr_libinput_get_device_handle(hwd_device->wlr_device);
    struct udev_device *udev_device = libinput_device_get_udev_device(device);
    if (!udev_device) {
        return false;
    }

    const char *id_path = udev_device_get_property_value(udev_device, "ID_PATH");
    if (!id_path) {
        return false;
    }

    const char prefix_platform[] = "platform-";
    if (strncmp(id_path, prefix_platform, strlen(prefix_platform)) != 0) {
        return false;
    }

    const char prefix_pci[] = "pci-";
    const char infix_platform[] = "-platform-";
    return (strncmp(id_path, prefix_pci, strlen(prefix_pci)) == 0) &&
        strstr(id_path, infix_platform);
}
