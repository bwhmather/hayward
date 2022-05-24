#include "wmiiv/config.h"
#include "wmiiv/input/switch.h"
#include <wlr/types/wlr_idle.h>
#include "log.h"

struct wmiiv_switch *wmiiv_switch_create(struct wmiiv_seat *seat,
		struct wmiiv_seat_device *device) {
	struct wmiiv_switch *switch_device =
		calloc(1, sizeof(struct wmiiv_switch));
	if (!wmiiv_assert(switch_device, "could not allocate switch")) {
		return NULL;
	}
	device->switch_device = switch_device;
	switch_device->seat_device = device;
	switch_device->state = WLR_SWITCH_STATE_OFF;
	wl_list_init(&switch_device->switch_toggle.link);
	wmiiv_log(WMIIV_DEBUG, "Allocated switch for device");

	return switch_device;
}

static bool wmiiv_switch_trigger_test(enum wmiiv_switch_trigger trigger,
		enum wlr_switch_state state) {
	switch (trigger) {
	case WMIIV_SWITCH_TRIGGER_ON:
		return state == WLR_SWITCH_STATE_ON;
	case WMIIV_SWITCH_TRIGGER_OFF:
		return state == WLR_SWITCH_STATE_OFF;
	case WMIIV_SWITCH_TRIGGER_TOGGLE:
		return true;
	}
	abort(); // unreachable
}

static void execute_binding(struct wmiiv_switch *wmiiv_switch) {
	struct wmiiv_seat* seat = wmiiv_switch->seat_device->wmiiv_seat;
	bool input_inhibited = seat->exclusive_client != NULL ||
		server.session_lock.locked;

	list_t *bindings = config->current_mode->switch_bindings;
	struct wmiiv_switch_binding *matched_binding = NULL;
	for (int i = 0; i < bindings->length; ++i) {
		struct wmiiv_switch_binding *binding = bindings->items[i];
		if (binding->type != wmiiv_switch->type) {
			continue;
		}
		if (!wmiiv_switch_trigger_test(binding->trigger, wmiiv_switch->state)) {
			continue;
		}
		if (config->reloading && (binding->trigger == WMIIV_SWITCH_TRIGGER_TOGGLE
				|| (binding->flags & BINDING_RELOAD) == 0)) {
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
		struct wmiiv_binding *dummy_binding =
			calloc(1, sizeof(struct wmiiv_binding));
		dummy_binding->type = BINDING_SWITCH;
		dummy_binding->flags = matched_binding->flags;
		dummy_binding->command = matched_binding->command;

		seat_execute_command(seat, dummy_binding);
		free(dummy_binding);
	}
}

static void handle_switch_toggle(struct wl_listener *listener, void *data) {
	struct wmiiv_switch *wmiiv_switch =
			wl_container_of(listener, wmiiv_switch, switch_toggle);
	struct wlr_switch_toggle_event *event = data;
	struct wmiiv_seat *seat = wmiiv_switch->seat_device->wmiiv_seat;
	seat_idle_notify_activity(seat, IDLE_SOURCE_SWITCH);

	struct wlr_input_device *wlr_device =
		wmiiv_switch->seat_device->input_device->wlr_device;
	char *device_identifier = input_device_get_identifier(wlr_device);
	wmiiv_log(WMIIV_DEBUG, "%s: type %d state %d", device_identifier,
			event->switch_type, event->switch_state);
	free(device_identifier);

	wmiiv_switch->type = event->switch_type;
	wmiiv_switch->state = event->switch_state;
	execute_binding(wmiiv_switch);
}

void wmiiv_switch_configure(struct wmiiv_switch *wmiiv_switch) {
	struct wlr_input_device *wlr_device =
		wmiiv_switch->seat_device->input_device->wlr_device;
	wl_list_remove(&wmiiv_switch->switch_toggle.link);
	wl_signal_add(&wlr_device->switch_device->events.toggle,
			&wmiiv_switch->switch_toggle);
	wmiiv_switch->switch_toggle.notify = handle_switch_toggle;
	wmiiv_log(WMIIV_DEBUG, "Configured switch for device");
}

void wmiiv_switch_destroy(struct wmiiv_switch *wmiiv_switch) {
	if (!wmiiv_switch) {
		return;
	}
	wl_list_remove(&wmiiv_switch->switch_toggle.link);
	free(wmiiv_switch);
}

void wmiiv_switch_retrigger_bindings_for_all(void) {
	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct wmiiv_seat_device *seat_device;
		wl_list_for_each(seat_device, &seat->devices, link) {
			struct wmiiv_input_device *input_device = seat_device->input_device;
			if (input_device->wlr_device->type != WLR_INPUT_DEVICE_SWITCH) {
				continue;
			}
			execute_binding(seat_device->switch_device);
		};
	}
}
