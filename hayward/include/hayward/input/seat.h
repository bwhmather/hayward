#ifndef HAYWARD_INPUT_SEAT_H
#define HAYWARD_INPUT_SEAT_H

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/util/edges.h>

#include <hayward-common/list.h>

#include <hayward/config.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/tablet.h>
#include <hayward/input/text_input.h>

struct hayward_seat;

struct hayward_seatop_impl {
    void (*button
    )(struct hayward_seat *seat, uint32_t time_msec,
      struct wlr_input_device *device, uint32_t button,
      enum wlr_button_state state);
    void (*pointer_motion)(struct hayward_seat *seat, uint32_t time_msec);
    void (*pointer_axis
    )(struct hayward_seat *seat, struct wlr_pointer_axis_event *event);
    void (*rebase)(struct hayward_seat *seat, uint32_t time_msec);
    void (*tablet_tool_motion
    )(struct hayward_seat *seat, struct hayward_tablet_tool *tool,
      uint32_t time_msec);
    void (*tablet_tool_tip
    )(struct hayward_seat *seat, struct hayward_tablet_tool *tool,
      uint32_t time_msec, enum wlr_tablet_tool_tip_state state);
    void (*end)(struct hayward_seat *seat);
    void (*unref)(struct hayward_seat *seat, struct hayward_window *container);
    void (*render
    )(struct hayward_seat *seat, struct hayward_output *output,
      pixman_region32_t *damage);
    bool allow_set_cursor;
};

struct hayward_seat_device {
    struct hayward_seat *hayward_seat;
    struct hayward_input_device *input_device;
    struct hayward_keyboard *keyboard;
    struct hayward_switch *switch_device;
    struct hayward_tablet *tablet;
    struct hayward_tablet_pad *tablet_pad;
    struct wl_list link; // hayward_seat::devices
};

struct hayward_drag_icon {
    struct hayward_seat *seat;
    struct wlr_drag_icon *wlr_drag_icon;
    struct wl_list link; // hayward_root::drag_icons

    double x, y; // in layout-local coordinates

    struct wl_listener surface_commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};

struct hayward_drag {
    struct hayward_seat *seat;
    struct wlr_drag *wlr_drag;
    struct wl_listener destroy;
};

struct hayward_seat {
    struct wlr_seat *wlr_seat;
    struct hayward_cursor *cursor;

    // The surface that is currently receiving input events.
    struct wlr_surface *focused_surface;

    // If exclusive_client is set, no other clients will receive input events.
    struct wl_client *exclusive_client;

    // Last touch point
    int32_t touch_id;
    double touch_x, touch_y;

    // Seat operations (drag and resize)
    const struct hayward_seatop_impl *seatop_impl;
    void *seatop_data;

    uint32_t last_button_serial;

    uint32_t idle_inhibit_sources, idle_wake_sources;

    list_t *deferred_bindings; // struct hayward_binding

    struct hayward_input_method_relay im_relay;

    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener transaction_before_commit;

    struct wl_list devices;         // hayward_seat_device::link
    struct wl_list keyboard_groups; // hayward_keyboard_group::link
    struct wl_list keyboard_shortcuts_inhibitors;
    // hayward_keyboard_shortcuts_inhibitor::link

    struct wl_list link; // input_manager::seats
};

struct hayward_pointer_constraint {
    struct hayward_cursor *cursor;
    struct wlr_pointer_constraint_v1 *constraint;

    struct wl_listener set_region;
    struct wl_listener destroy;
};

struct hayward_keyboard_shortcuts_inhibitor {
    struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;

    struct wl_listener destroy;

    struct wl_list link; // hayward_seat::keyboard_shortcuts_inhibitors
};

struct hayward_seat *
seat_create(const char *seat_name);

void
seat_destroy(struct hayward_seat *seat);

void
seat_idle_notify_activity(
    struct hayward_seat *seat, enum hayward_input_idle_source source
);

bool
seat_is_input_allowed(struct hayward_seat *seat, struct wlr_surface *surface);

void
drag_icon_update_position(struct hayward_drag_icon *icon);

void
seat_configure_device(
    struct hayward_seat *seat, struct hayward_input_device *device
);

void
seat_reset_device(
    struct hayward_seat *seat, struct hayward_input_device *input_device
);

void
seat_add_device(struct hayward_seat *seat, struct hayward_input_device *device);

void
seat_remove_device(
    struct hayward_seat *seat, struct hayward_input_device *device
);

void
seat_configure_xcursor(struct hayward_seat *seat);

// Force focus to a particular surface that is not part of the workspace
// hierarchy (used for lockscreen)
void
hayward_force_focus(struct wlr_surface *surface);

void
seat_set_exclusive_client(struct hayward_seat *seat, struct wl_client *client);

void
seat_apply_config(struct hayward_seat *seat, struct seat_config *seat_config);

struct seat_config *
seat_get_config(struct hayward_seat *seat);

struct seat_config *
seat_get_config_by_name(const char *name);

enum wlr_edges
find_resize_edge(
    struct hayward_window *cont, struct wlr_surface *surface,
    struct hayward_cursor *cursor
);

void
seat_pointer_notify_button(
    struct hayward_seat *seat, uint32_t time_msec, uint32_t button,
    enum wlr_button_state state
);

void
seatop_button(
    struct hayward_seat *seat, uint32_t time_msec,
    struct wlr_input_device *device, uint32_t button,
    enum wlr_button_state state
);

void
seatop_pointer_motion(struct hayward_seat *seat, uint32_t time_msec);

void
seatop_pointer_axis(
    struct hayward_seat *seat, struct wlr_pointer_axis_event *event
);

void
seatop_tablet_tool_tip(
    struct hayward_seat *seat, struct hayward_tablet_tool *tool,
    uint32_t time_msec, enum wlr_tablet_tool_tip_state state
);

void
seatop_tablet_tool_motion(
    struct hayward_seat *seat, struct hayward_tablet_tool *tool,
    uint32_t time_msec
);

void
seatop_rebase(struct hayward_seat *seat, uint32_t time_msec);

/**
 * End a seatop (ie. free any seatop specific resources).
 */
void
seatop_end(struct hayward_seat *seat);

/**
 * Instructs the seatop implementation to drop any references to the given
 * container (eg. because the container is destroying).
 * The seatop may choose to abort itself in response to this.
 */
void
seatop_unref(struct hayward_seat *seat, struct hayward_window *container);

bool
seatop_allows_set_cursor(struct hayward_seat *seat);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the given surface
 * or NULL if none exists.
 */
struct hayward_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(
    const struct hayward_seat *seat, const struct wlr_surface *surface
);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the currently
 * focused surface of a seat or NULL if none exists.
 */
struct hayward_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(
    const struct hayward_seat *seat
);

#endif
