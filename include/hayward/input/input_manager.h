#ifndef HWD_INPUT_INPUT_MANAGER_H
#define HWD_INPUT_INPUT_MANAGER_H

#include <stdbool.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>

struct hwd_input_device {
    char *identifier;
    struct wlr_input_device *wlr_device;
    struct wl_list link;
    struct wl_listener device_destroy;
    bool is_virtual;
};

struct hwd_input_manager {
    struct wl_list devices;
    struct wl_list seats;

    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *keyboard_shortcuts_inhibit;
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard;
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer;

    struct wl_listener new_input;
    struct wl_listener keyboard_shortcuts_inhibit_new_inhibitor;
    struct wl_listener virtual_keyboard_new;
    struct wl_listener virtual_pointer_new;
};

/**
 * Gets the last seat the user interacted with
 */
struct hwd_seat *
input_manager_current_seat(void);

struct hwd_seat *
input_manager_get_default_seat(void);

struct hwd_seat *
input_manager_get_seat(const char *seat_name, bool create);

char *
input_device_get_identifier(struct wlr_input_device *device);

const char *
input_device_get_type(struct hwd_input_device *device);

/**
 * If none of the seat configs have a fallback setting (either true or false),
 * create the default seat (if needed) and set it as the fallback
 */
void
input_manager_verify_fallback_seat(void);

struct hwd_input_manager *
input_manager_create(struct wl_display *wl_display, struct wlr_backend *backend);

void
input_manager_configure_all_inputs(void);

void
input_manager_apply_input_config(struct input_config *input_config);

void
input_manager_reset_all_inputs(void);

void
input_manager_apply_seat_config(struct seat_config *seat_config);

void
input_manager_configure_xcursor(void);

struct input_config *
input_device_get_config(struct hwd_input_device *device);

#endif
