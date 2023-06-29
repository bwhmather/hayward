#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/input/switch.h"

#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_switch.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/server.h>
#include <hayward/transaction.h>

#include <config.h>

struct hayward_switch *
hayward_switch_create(
    struct hayward_seat *seat, struct hayward_seat_device *device
) {
    struct hayward_switch *switch_device =
        calloc(1, sizeof(struct hayward_switch));
    hayward_assert(switch_device, "could not allocate switch");
    device->switch_device = switch_device;
    switch_device->seat_device = device;
    switch_device->wlr =
        wlr_switch_from_input_device(device->input_device->wlr_device);
    switch_device->state = WLR_SWITCH_STATE_OFF;
    wl_list_init(&switch_device->switch_toggle.link);
    hayward_log(HAYWARD_DEBUG, "Allocated switch for device");

    return switch_device;
}

static bool
hayward_switch_trigger_test(
    enum hayward_switch_trigger trigger, enum wlr_switch_state state
) {
    switch (trigger) {
    case HAYWARD_SWITCH_TRIGGER_ON:
        return state == WLR_SWITCH_STATE_ON;
    case HAYWARD_SWITCH_TRIGGER_OFF:
        return state == WLR_SWITCH_STATE_OFF;
    case HAYWARD_SWITCH_TRIGGER_TOGGLE:
        return true;
    }
    abort(); // unreachable
}

static void
execute_binding(struct hayward_switch *hayward_switch) {
    struct hayward_seat *seat = hayward_switch->seat_device->hayward_seat;
    bool input_inhibited =
        seat->exclusive_client != NULL || server.session_lock.locked;

    list_t *bindings = config->current_mode->switch_bindings;
    struct hayward_switch_binding *matched_binding = NULL;
    for (int i = 0; i < bindings->length; ++i) {
        struct hayward_switch_binding *binding = bindings->items[i];
        if (binding->type != hayward_switch->type) {
            continue;
        }
        if (!hayward_switch_trigger_test(
                binding->trigger, hayward_switch->state
            )) {
            continue;
        }
        if (config->reloading &&
            (binding->trigger == HAYWARD_SWITCH_TRIGGER_TOGGLE ||
             (binding->flags & BINDING_RELOAD) == 0)) {
            continue;
        }
        bool binding_locked = binding->flags & BINDING_LOCKED;
        if (!binding_locked && input_inhibited) {
            continue;
        }

        matched_binding = binding;

        if (binding_locked == input_inhibited) {
            break;
        }
    }

    if (matched_binding) {
        struct hayward_binding *dummy_binding =
            calloc(1, sizeof(struct hayward_binding));
        dummy_binding->type = BINDING_SWITCH;
        dummy_binding->flags = matched_binding->flags;
        dummy_binding->command = matched_binding->command;

        seat_execute_command(seat, dummy_binding);
        free(dummy_binding);
    }
}

static void
handle_switch_toggle(struct wl_listener *listener, void *data) {
    struct hayward_switch *hayward_switch =
        wl_container_of(listener, hayward_switch, switch_toggle);
    struct wlr_switch_toggle_event *event = data;

    transaction_begin();

    struct hayward_seat *seat = hayward_switch->seat_device->hayward_seat;
    seat_idle_notify_activity(seat, IDLE_SOURCE_SWITCH);

    struct wlr_input_device *wlr_device =
        hayward_switch->seat_device->input_device->wlr_device;
    char *device_identifier = input_device_get_identifier(wlr_device);
    hayward_log(
        HAYWARD_DEBUG, "%s: type %d state %d", device_identifier,
        event->switch_type, event->switch_state
    );
    free(device_identifier);

    hayward_switch->type = event->switch_type;
    hayward_switch->state = event->switch_state;
    execute_binding(hayward_switch);

    transaction_end();
}

void
hayward_switch_configure(struct hayward_switch *hayward_switch) {
    wl_list_remove(&hayward_switch->switch_toggle.link);
    wl_signal_add(
        &hayward_switch->wlr->events.toggle, &hayward_switch->switch_toggle
    );
    hayward_switch->switch_toggle.notify = handle_switch_toggle;
    hayward_log(HAYWARD_DEBUG, "Configured switch for device");
}

void
hayward_switch_destroy(struct hayward_switch *hayward_switch) {
    if (!hayward_switch) {
        return;
    }
    wl_list_remove(&hayward_switch->switch_toggle.link);
    free(hayward_switch);
}

void
hayward_switch_retrigger_bindings_for_all(void) {
    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        struct hayward_seat_device *seat_device;
        wl_list_for_each(seat_device, &seat->devices, link) {
            struct hayward_input_device *input_device =
                seat_device->input_device;
            if (input_device->wlr_device->type != WLR_INPUT_DEVICE_SWITCH) {
                continue;
            }
            execute_binding(seat_device->switch_device);
        };
    }
}
