#ifndef _HAYWARD_INPUT_KEYBOARD_H
#define _HAYWARD_INPUT_KEYBOARD_H

#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>

#define HAYWARD_KEYBOARD_PRESSED_KEYS_CAP 32

/**
 * Get modifier mask from modifier name.
 *
 * Returns the modifier mask or 0 if the name isn't found.
 */
uint32_t
get_modifier_mask_by_name(const char *name);

/**
 * Get modifier name from modifier mask.
 *
 * Returns the modifier name or NULL if it isn't found.
 */
const char *
get_modifier_name_by_mask(uint32_t modifier);

/**
 * Get an array of modifier names from modifier_masks
 *
 * Populates the names array and return the number of names added.
 */
int
get_modifier_names(const char **names, uint32_t modifier_masks);

struct hayward_shortcut_state {
    /**
     * A list of pressed key ids (either keysyms or keycodes),
     * including duplicates when different keycodes produce the same key id.
     *
     * Each key id is associated with the keycode (in `pressed_keycodes`)
     * whose press generated it, so that the key id can be removed on
     * keycode release without recalculating the transient link between
     * keycode and key id at the time of the key press.
     */
    uint32_t pressed_keys[HAYWARD_KEYBOARD_PRESSED_KEYS_CAP];
    /**
     * The list of keycodes associated to currently pressed key ids,
     * including duplicates when a keycode generates multiple key ids.
     */
    uint32_t pressed_keycodes[HAYWARD_KEYBOARD_PRESSED_KEYS_CAP];
    uint32_t last_keycode;
    uint32_t last_raw_modifiers;
    size_t npressed;
    uint32_t current_key;
};

struct hayward_keyboard {
    struct hayward_seat_device *seat_device;

    struct xkb_keymap *keymap;
    xkb_layout_index_t effective_layout;

    int32_t repeat_rate;
    int32_t repeat_delay;

    struct wl_listener keyboard_key;
    struct wl_listener keyboard_modifiers;

    struct hayward_shortcut_state state_keysyms_translated;
    struct hayward_shortcut_state state_keysyms_raw;
    struct hayward_shortcut_state state_keycodes;
    struct hayward_shortcut_state state_pressed_sent;
    struct hayward_binding *held_binding;

    struct wl_event_source *key_repeat_source;
    struct hayward_binding *repeat_binding;
};

struct hayward_keyboard_group {
    struct wlr_keyboard_group *wlr_group;
    struct hayward_seat_device *seat_device;
    struct wl_listener keyboard_key;
    struct wl_listener keyboard_modifiers;
    struct wl_listener enter;
    struct wl_listener leave;
    struct wl_list link; // hayward_seat::keyboard_groups
};

struct xkb_keymap *
hayward_keyboard_compile_keymap(struct input_config *ic, char **error);

struct hayward_keyboard *
hayward_keyboard_create(
    struct hayward_seat *seat, struct hayward_seat_device *device
);

void
hayward_keyboard_configure(struct hayward_keyboard *keyboard);

void
hayward_keyboard_destroy(struct hayward_keyboard *keyboard);

void
hayward_keyboard_disarm_key_repeat(struct hayward_keyboard *keyboard);
#endif
