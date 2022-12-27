#ifndef _HAYWARD_INPUT_CURSOR_H
#define _HAYWARD_INPUT_CURSOR_H
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
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>

#include <hayward/config.h>
#include <hayward/input/seat.h>

#include <config.h>

#define HAYWARD_CURSOR_PRESSED_BUTTONS_CAP 32

#define HAYWARD_SCROLL_UP KEY_MAX + 1
#define HAYWARD_SCROLL_DOWN KEY_MAX + 2
#define HAYWARD_SCROLL_LEFT KEY_MAX + 3
#define HAYWARD_SCROLL_RIGHT KEY_MAX + 4

struct hayward_cursor {
    struct hayward_seat *seat;
    struct wlr_cursor *cursor;
    struct {
        double x, y;
        struct hayward_node *node;
    } previous;
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

    struct wl_event_source *hide_source;
    bool hidden;
    // This field is just a cache of the field in seat_config in order to avoid
    // costly seat_config lookups on every keypress. HIDE_WHEN_TYPING_DEFAULT
    // indicates that there is no cached value.
    enum seat_config_hide_cursor_when_typing hide_when_typing;

    size_t pressed_button_count;
};

struct hayward_workspace;
struct hayward_window;

void
seat_get_target_at(
    struct hayward_seat *seat, double lx, double ly,
    struct hayward_output **output_out, struct hayward_window **window_out,
    struct wlr_surface **surface_out, double *sx_out, double *sy_out
);

/**
 * "Rebase" a cursor on top of whatever view is underneath it.
 *
 * This chooses a cursor icon and sends a motion event to the surface.
 */
void
cursor_rebase(struct hayward_cursor *cursor);
void
cursor_rebase_all(void);
void
cursor_update_image(struct hayward_cursor *cursor, struct hayward_node *node);

int
cursor_get_timeout(struct hayward_cursor *cursor);

void
cursor_notify_key_press(struct hayward_cursor *cursor);

void
cursor_handle_activity_from_idle_source(
    struct hayward_cursor *cursor, enum hayward_input_idle_source idle_source
);

void
cursor_handle_activity_from_device(
    struct hayward_cursor *cursor, struct wlr_input_device *device
);

void
cursor_unhide(struct hayward_cursor *cursor);

void
dispatch_cursor_button(
    struct hayward_cursor *cursor, struct wlr_input_device *device,
    uint32_t time_msec, uint32_t button, enum wlr_button_state state
);

void
dispatch_cursor_axis(
    struct hayward_cursor *cursor, struct wlr_pointer_axis_event *event
);

void
cursor_set_image(
    struct hayward_cursor *cursor, const char *image, struct wl_client *client
);

void
cursor_set_image_surface(
    struct hayward_cursor *cursor, struct wlr_surface *surface,
    int32_t hotspot_x, int32_t hotspot_y, struct wl_client *client
);

void
hayward_cursor_destroy(struct hayward_cursor *cursor);
struct hayward_cursor *
hayward_cursor_create(struct hayward_seat *seat);

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
hayward_cursor_constrain(
    struct hayward_cursor *cursor, struct wlr_pointer_constraint_v1 *constraint
);

#endif
