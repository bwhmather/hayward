#ifndef _HAYWARD_INPUT_SEAT_H
#define _HAYWARD_INPUT_SEAT_H

#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include "hayward/config.h"
#include "hayward/input/input-manager.h"
#include "hayward/input/tablet.h"
#include "hayward/input/text_input.h"

struct hayward_seat;

struct hayward_seatop_impl {
	void (*button)(struct hayward_seat *seat, uint32_t time_msec,
			struct wlr_input_device *device, uint32_t button,
			enum wlr_button_state state);
	void (*pointer_motion)(struct hayward_seat *seat, uint32_t time_msec);
	void (*pointer_axis)(struct hayward_seat *seat,
			struct wlr_pointer_axis_event *event);
	void (*rebase)(struct hayward_seat *seat, uint32_t time_msec);
	void (*tablet_tool_motion)(struct hayward_seat *seat,
			struct hayward_tablet_tool *tool, uint32_t time_msec);
	void (*tablet_tool_tip)(struct hayward_seat *seat, struct hayward_tablet_tool *tool,
			uint32_t time_msec, enum wlr_tablet_tool_tip_state state);
	void (*end)(struct hayward_seat *seat);
	void (*unref)(struct hayward_seat *seat, struct hayward_window *container);
	void (*render)(struct hayward_seat *seat, struct hayward_output *output,
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

struct hayward_seat_workspace {
	struct hayward_seat *seat;
	struct hayward_workspace *workspace;

	struct wl_list link;  // hayward_seat::active_workspace_stack

	struct wl_listener destroy;
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

	// True if a window, in particular the window at the top of the active
	// window stack, has focus.
	bool has_focus;

	// List of workspaces in focus order.  If the seat has a focused window, the
	// top of the stack will match the workspace for that window.
	struct wl_list active_workspace_stack;

	// The window that is currently receiving input events.
	struct hayward_window *focused_window;

	// If the focused layer is set, views cannot receive keyboard focus.
	struct wlr_layer_surface_v1 *focused_layer;

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

	struct wl_listener new_node;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;

	struct wl_list devices; // hayward_seat_device::link
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

struct hayward_seat *seat_create(const char *seat_name);

void seat_destroy(struct hayward_seat *seat);

void seat_add_device(struct hayward_seat *seat,
		struct hayward_input_device *device);

void seat_configure_device(struct hayward_seat *seat,
		struct hayward_input_device *device);

void seat_reset_device(struct hayward_seat *seat,
		struct hayward_input_device *input_device);

void seat_remove_device(struct hayward_seat *seat,
		struct hayward_input_device *device);

void seat_configure_xcursor(struct hayward_seat *seat);

/**
 * Redirects input events to the window or surface currently marked as focused
 * in the tree.
 */
void seat_commit_focus(struct hayward_seat *seat);

void seat_set_focus_surface(struct hayward_seat *seat,
		struct wlr_surface *surface, bool unfocus);

void seat_set_focus_layer(struct hayward_seat *seat,
		struct wlr_layer_surface_v1 *layer);

void seat_set_exclusive_client(struct hayward_seat *seat,
		struct wl_client *client);

struct hayward_node *seat_get_focus(struct hayward_seat *seat);

struct hayward_window *seat_get_focused_container(struct hayward_seat *seat);

// Force focus to a particular surface that is not part of the workspace
// hierarchy (used for lockscreen)
void hayward_force_focus(struct wlr_surface *surface);

struct hayward_window *seat_get_active_window_for_column(struct hayward_seat *seat, struct hayward_column *column);

struct hayward_window *seat_get_active_tiling_window_for_workspace(struct hayward_seat *seat, struct hayward_workspace *workspace);

struct hayward_window *seat_get_active_floating_window_for_workspace(struct hayward_seat *seat, struct hayward_workspace *workspace);

struct hayward_window *seat_get_active_window_for_workspace(struct hayward_seat *seat, struct hayward_workspace *workspace);

/**
 * Return the last container to be focused for the seat (or the most recently
 * opened if no container has received focused) that is a child of the given
 * container. The focus-inactive container of the root window is the focused
 * container for the seat (if the seat does have focus). This function can be
 * used to determine what container gets focused next if the focused container
 * is destroyed, or focus moves to a container with children and we need to
 * descend into the next leaf in focus order.
 */
struct hayward_node *seat_get_focus_inactive(struct hayward_seat *seat,
		struct hayward_node *node);

void seat_apply_config(struct hayward_seat *seat, struct seat_config *seat_config);

struct seat_config *seat_get_config(struct hayward_seat *seat);

struct seat_config *seat_get_config_by_name(const char *name);

void seat_idle_notify_activity(struct hayward_seat *seat,
		enum hayward_input_idle_source source);

bool seat_is_input_allowed(struct hayward_seat *seat, struct wlr_surface *surface);

void drag_icon_update_position(struct hayward_drag_icon *icon);

enum wlr_edges find_resize_edge(struct hayward_window *cont,
		struct wlr_surface *surface, struct hayward_cursor *cursor);

void seatop_begin_default(struct hayward_seat *seat);

void seatop_begin_down(struct hayward_seat *seat, struct hayward_window *container,
		uint32_t time_msec, double sx, double sy);

void seatop_begin_down_on_surface(struct hayward_seat *seat,
		struct wlr_surface *surface, uint32_t time_msec, double sx, double sy);

void seatop_begin_move_floating(struct hayward_seat *seat,
		struct hayward_window *container);

void seatop_begin_move_tiling_threshold(struct hayward_seat *seat,
		struct hayward_window *container);

void seatop_begin_move_tiling(struct hayward_seat *seat,
		struct hayward_window *container);

void seatop_begin_resize_floating(struct hayward_seat *seat,
		struct hayward_window *container, enum wlr_edges edge);

void seatop_begin_resize_tiling(struct hayward_seat *seat,
		struct hayward_window *container, enum wlr_edges edge);

void seat_pointer_notify_button(struct hayward_seat *seat, uint32_t time_msec,
		uint32_t button, enum wlr_button_state state);

void seatop_button(struct hayward_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state);

void seatop_pointer_motion(struct hayward_seat *seat, uint32_t time_msec);

void seatop_pointer_axis(struct hayward_seat *seat,
		struct wlr_pointer_axis_event *event);

void seatop_tablet_tool_tip(struct hayward_seat *seat,
		struct hayward_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state);

void seatop_tablet_tool_motion(struct hayward_seat *seat,
		struct hayward_tablet_tool *tool, uint32_t time_msec);

void seatop_rebase(struct hayward_seat *seat, uint32_t time_msec);

/**
 * End a seatop (ie. free any seatop specific resources).
 */
void seatop_end(struct hayward_seat *seat);

/**
 * Instructs the seatop implementation to drop any references to the given
 * container (eg. because the container is destroying).
 * The seatop may choose to abort itself in response to this.
 */
void seatop_unref(struct hayward_seat *seat, struct hayward_window *container);

/**
 * Instructs a seatop to render anything that it needs to render
 * (eg. dropzone for move-tiling)
 */
void seatop_render(struct hayward_seat *seat, struct hayward_output *output,
		pixman_region32_t *damage);

bool seatop_allows_set_cursor(struct hayward_seat *seat);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the given surface
 * or NULL if none exists.
 */
struct hayward_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(const struct hayward_seat *seat,
		const struct wlr_surface *surface);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the currently
 * focused surface of a seat or NULL if none exists.
 */
struct hayward_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(const struct hayward_seat *seat);

#endif
