#ifndef _HAYWARD_INPUT_TEXT_INPUT_H
#define _HAYWARD_INPUT_TEXT_INPUT_H

#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_compositor.h>
#include "hayward/input/seat.h"

/**
 * The relay structure manages the relationship between text-input and
 * input_method interfaces on a given seat. Multiple text-input interfaces may
 * be bound to a relay, but at most one will be focused (reveiving events) at
 * a time. At most one input-method interface may be bound to the seat. The
 * relay manages life cycle of both sides. When both sides are present and
 * focused, the relay passes messages between them.
 *
 * Text input focus is a subset of keyboard focus - if the text-input is
 * in the focused state, wl_keyboard sent an enter as well. However, having
 * wl_keyboard focused doesn't mean that text-input will be focused.
 */
struct hayward_input_method_relay {
	struct hayward_seat *seat;

	struct wl_list text_inputs; // hayward_text_input::link
	struct wlr_input_method_v2 *input_method; // doesn't have to be present

	struct wl_listener text_input_new;

	struct wl_listener input_method_new;
	struct wl_listener input_method_commit;
	struct wl_listener input_method_grab_keyboard;
	struct wl_listener input_method_destroy;

	struct wl_listener input_method_keyboard_grab_destroy;
};

struct hayward_text_input {
	struct hayward_input_method_relay *relay;

	struct wlr_text_input_v3 *input;
	// The surface getting seat's focus. Stored for when text-input cannot
	// be sent an enter event immediately after getting focus, e.g. when
	// there's no input method available. Cleared once text-input is entered.
	struct wlr_surface *pending_focused_surface;

	struct wl_list link;

	struct wl_listener pending_focused_surface_destroy;

	struct wl_listener text_input_enable;
	struct wl_listener text_input_commit;
	struct wl_listener text_input_disable;
	struct wl_listener text_input_destroy;
};

void hayward_input_method_relay_init(struct hayward_seat *seat,
	struct hayward_input_method_relay *relay);

void hayward_input_method_relay_finish(struct hayward_input_method_relay *relay);

// Updates currently focused surface. Surface must belong to the same seat.
void hayward_input_method_relay_set_focus(struct hayward_input_method_relay *relay,
	struct wlr_surface *surface);

struct hayward_text_input *hayward_text_input_create(
	struct hayward_input_method_relay *relay,
	struct wlr_text_input_v3 *text_input);

#endif
