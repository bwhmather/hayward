#ifndef HWD_INPUT_CURSOR_H
#define HWD_INPUT_CURSOR_H

#include <config.h>

#include <linux/input-event-codes.h>
#include <pixman.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>
#include <hayward/tree/output.h>

#define HWD_CURSOR_PRESSED_BUTTONS_CAP 32

#define HWD_SCROLL_UP KEY_MAX + 1
#define HWD_SCROLL_DOWN KEY_MAX + 2
#define HWD_SCROLL_LEFT KEY_MAX + 3
#define HWD_SCROLL_RIGHT KEY_MAX + 4

struct hwd_cursor {
    struct hwd_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor_manager;
    struct wl_list tablets;
    struct wl_list tablet_pads;

    const char *image;
    struct wl_client *image_client;
    struct wlr_surface *image_surface;
    int hotspot_x, hotspot_y;

    struct wlr_pointer_constraint_v1 *active_constraint;
    pixman_region32_t confine; // invalid if active_constraint == NULL
    bool active_confine_requires_warp;

    struct wlr_pointer_gestures_v1 *pointer_gestures;
    struct wl_listener pinch_begin;
    struct wl_listener pinch_update;
    struct wl_listener pinch_end;
    struct wl_listener swipe_begin;
    struct wl_listener swipe_update;
    struct wl_listener swipe_end;
    struct wl_listener hold_begin;
    struct wl_listener hold_end;

    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;

    struct wl_listener touch_down;
    struct wl_listener touch_up;
    struct wl_listener touch_motion;
    struct wl_listener touch_frame;
    bool simulating_pointer_from_touch;
    bool pointer_touch_up;
    int32_t pointer_touch_id;

    struct wl_listener tool_axis;
    struct wl_listener tool_tip;
    struct wl_listener tool_proximity;
    struct wl_listener tool_button;
    bool simulating_pointer_from_tool_tip;
    uint32_t tool_buttons;

    struct wl_listener request_set_cursor;
    struct wl_listener image_surface_destroy;

    struct wl_listener constraint_commit;

    struct wl_listener root_scene_changed;

    struct wl_event_source *hide_source;
    bool hidden;
    // This field is just a cache of the field in seat_config in order to avoid
    // costly seat_config lookups on every keypress. HIDE_WHEN_TYPING_DEFAULT
    // indicates that there is no cached value.
    enum seat_config_hide_cursor_when_typing hide_when_typing;

    size_t pressed_button_count;
};

struct hwd_workspace;
struct hwd_window;

void
seat_get_target_at(
    struct hwd_seat *seat, double lx, double ly, struct hwd_output **output_out,
    struct hwd_window **window_out, struct wlr_surface **surface_out, double *sx_out, double *sy_out
);

int
cursor_get_timeout(struct hwd_cursor *cursor);

void
cursor_notify_key_press(struct hwd_cursor *cursor);

void
cursor_handle_activity_from_device(struct hwd_cursor *cursor, struct wlr_input_device *device);

void
cursor_set_image(struct hwd_cursor *cursor, const char *image, struct wl_client *client);

void
cursor_set_image_surface(
    struct hwd_cursor *cursor, struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y,
    struct wl_client *client
);

void
hwd_cursor_destroy(struct hwd_cursor *cursor);
struct hwd_cursor *
hwd_cursor_create(struct hwd_seat *seat);

uint32_t
get_mouse_bindsym(const char *name, char **error);

uint32_t
get_mouse_bindcode(const char *name, char **error);

// Considers both bindsym and bindcode
uint32_t
get_mouse_button(const char *name, char **error);

const char *
get_mouse_button_name(uint32_t button);

void
hwd_cursor_constrain(struct hwd_cursor *cursor, struct wlr_pointer_constraint_v1 *constraint);

#endif
