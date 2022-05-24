#ifndef _WMIIV_INPUT_KEYBOARD_H
#define _WMIIV_INPUT_KEYBOARD_H

#include "wmiiv/input/seat.h"

#define WMIIV_KEYBOARD_PRESSED_KEYS_CAP 32

/**
 * Get modifier mask from modifier name.
 *
 * Returns the modifier mask or 0 if the name isn't found.
 */
uint32_t get_modifier_mask_by_name(const char *name);

/**
 * Get modifier name from modifier mask.
 *
 * Returns the modifier name or NULL if it isn't found.
 */
const char *get_modifier_name_by_mask(uint32_t modifier);

/**
 * Get an array of modifier names from modifier_masks
 *
 * Populates the names array and return the number of names added.
 */
int get_modifier_names(const char **names, uint32_t modifier_masks);

struct wmiiv_shortcut_state {
	/**
	 * A list of pressed key ids (either keysyms or keycodes),
	 * including duplicates when different keycodes produce the same key id.
	 *
	 * Each key id is associated with the keycode (in `pressed_keycodes`)
	 * whose press generated it, so that the key id can be removed on
	 * keycode release without recalculating the transient link between
	 * keycode and key id at the time of the key press.
	 */
	uint32_t pressed_keys[WMIIV_KEYBOARD_PRESSED_KEYS_CAP];
	/**
	 * The list of keycodes associated to currently pressed key ids,
	 * including duplicates when a keycode generates multiple key ids.
	 */
	uint32_t pressed_keycodes[WMIIV_KEYBOARD_PRESSED_KEYS_CAP];
	uint32_t last_keycode;
	uint32_t last_raw_modifiers;
	size_t npressed;
	uint32_t current_key;
};

struct wmiiv_keyboard {
	struct wmiiv_seat_device *seat_device;

	struct xkb_keymap *keymap;
	xkb_layout_index_t effective_layout;

	int32_t repeat_rate;
	int32_t repeat_delay;

	struct wl_listener keyboard_key;
	struct wl_listener keyboard_modifiers;

	struct wmiiv_shortcut_state state_keysyms_translated;
	struct wmiiv_shortcut_state state_keysyms_raw;
	struct wmiiv_shortcut_state state_keycodes;
	struct wmiiv_shortcut_state state_pressed_sent;
	struct wmiiv_binding *held_binding;

	struct wl_event_source *key_repeat_source;
	struct wmiiv_binding *repeat_binding;
};

struct wmiiv_keyboard_group {
	struct wlr_keyboard_group *wlr_group;
	struct wmiiv_seat_device *seat_device;
	struct wl_listener keyboard_key;
	struct wl_listener keyboard_modifiers;
	struct wl_listener enter;
	struct wl_listener leave;
	struct wl_list link; // wmiiv_seat::keyboard_groups
};

struct xkb_keymap *wmiiv_keyboard_compile_keymap(struct input_config *ic,
		char **error);

struct wmiiv_keyboard *wmiiv_keyboard_create(struct wmiiv_seat *seat,
		struct wmiiv_seat_device *device);

void wmiiv_keyboard_configure(struct wmiiv_keyboard *keyboard);

void wmiiv_keyboard_destroy(struct wmiiv_keyboard *keyboard);

void wmiiv_keyboard_disarm_key_repeat(struct wmiiv_keyboard *keyboard);
#endif
