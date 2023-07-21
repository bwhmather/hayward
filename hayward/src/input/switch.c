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
#include <hayward/globals/transaction.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/server.h>
#include <hayward/transaction.h>

#include <config.h>

struct hwd_switch *
hwd_switch_create(struct hwd_seat *seat, struct hwd_seat_device *device) {
    struct hwd_switch *switch_device = calloc(1, sizeof(struct hwd_switch));
    hwd_assert(switch_device, "could not allocate switch");
    device->switch_device = switch_device;
    switch_device->seat_device = device;
    switch_device->wlr = wlr_switch_from_input_device(device->input_device->wlr_device);
    switch_device->state = WLR_SWITCH_STATE_OFF;
    wl_list_init(&switch_device->switch_toggle.link);
    hwd_log(HWD_DEBUG, "Allocated switch for device");

    return switch_device;
}

static bool
hwd_switch_trigger_test(enum hwd_switch_trigger trigger, enum wlr_switch_state state) {
    switch (trigger) {
    case HWD_SWITCH_TRIGGER_ON:
        return state == WLR_SWITCH_STATE_ON;
    case HWD_SWITCH_TRIGGER_OFF:
        return state == WLR_SWITCH_STATE_OFF;
    case HWD_SWITCH_TRIGGER_TOGGLE:
        return true;
    }
    abort(); // unreachable
}

static void
execute_binding(struct hwd_switch *hwd_switch) {
    struct hwd_seat *seat = hwd_switch->seat_device->hwd_seat;
    bool input_inhibited = seat->exclusive_client != NULL || server.session_lock.locked;

    list_t *bindings = config->current_mode->switch_bindings;
    struct hwd_switch_binding *matched_binding = NULL;
    for (int i = 0; i < bindings->length; ++i) {
        struct hwd_switch_binding *binding = bindings->items[i];
        if (binding->type != hwd_switch->type) {
            continue;
        }
        if (!hwd_switch_trigger_test(binding->trigger, hwd_switch->state)) {
            continue;
        }
        if (config->reloading &&
            (binding->trigger == HWD_SWITCH_TRIGGER_TOGGLE || (binding->flags & BINDING_RELOAD) == 0
            )) {
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
        struct hwd_binding *dummy_binding = calloc(1, sizeof(struct hwd_binding));
        dummy_binding->type = BINDING_SWITCH;
        dummy_binding->flags = matched_binding->flags;
        dummy_binding->command = matched_binding->command;

        seat_execute_command(seat, dummy_binding);
        free(dummy_binding);
    }
}

static void
handle_switch_toggle(struct wl_listener *listener, void *data) {
    struct hwd_switch *hwd_switch = wl_container_of(listener, hwd_switch, switch_toggle);
    struct wlr_switch_toggle_event *event = data;

    hwd_transaction_manager_begin_transaction(transaction_manager);

    struct hwd_seat *seat = hwd_switch->seat_device->hwd_seat;
    seat_idle_notify_activity(seat, IDLE_SOURCE_SWITCH);

    struct wlr_input_device *wlr_device = hwd_switch->seat_device->input_device->wlr_device;
    char *device_identifier = input_device_get_identifier(wlr_device);
    hwd_log(
        HWD_DEBUG, "%s: type %d state %d", device_identifier, event->switch_type,
        event->switch_state
    );
    free(device_identifier);

    hwd_switch->type = event->switch_type;
    hwd_switch->state = event->switch_state;
    execute_binding(hwd_switch);

    hwd_transaction_manager_end_transaction(transaction_manager);
}

void
hwd_switch_configure(struct hwd_switch *hwd_switch) {
    wl_list_remove(&hwd_switch->switch_toggle.link);
    wl_signal_add(&hwd_switch->wlr->events.toggle, &hwd_switch->switch_toggle);
    hwd_switch->switch_toggle.notify = handle_switch_toggle;
    hwd_log(HWD_DEBUG, "Configured switch for device");
}

void
hwd_switch_destroy(struct hwd_switch *hwd_switch) {
    if (!hwd_switch) {
        return;
    }
    wl_list_remove(&hwd_switch->switch_toggle.link);
    free(hwd_switch);
}

void
hwd_switch_retrigger_bindings_for_all(void) {
    struct hwd_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        struct hwd_seat_device *seat_device;
        wl_list_for_each(seat_device, &seat->devices, link) {
            struct hwd_input_device *input_device = seat_device->input_device;
            if (input_device->wlr_device->type != WLR_INPUT_DEVICE_SWITCH) {
                continue;
            }
            execute_binding(seat_device->switch_device);
        };
    }
}
