#ifndef _WMIIV_INPUT_SEAT_H
#define _WMIIV_INPUT_SEAT_H

#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/edges.h>
#include "wmiiv/config.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/tablet.h"
#include "wmiiv/input/text_input.h"

struct wmiiv_seat;

struct wmiiv_seatop_impl {
	void (*button)(struct wmiiv_seat *seat, uint32_t time_msec,
			struct wlr_input_device *device, uint32_t button,
			enum wlr_button_state state);
	void (*pointer_motion)(struct wmiiv_seat *seat, uint32_t time_msec);
	void (*pointer_axis)(struct wmiiv_seat *seat,
			struct wlr_pointer_axis_event *event);
	void (*rebase)(struct wmiiv_seat *seat, uint32_t time_msec);
	void (*tablet_tool_motion)(struct wmiiv_seat *seat,
			struct wmiiv_tablet_tool *tool, uint32_t time_msec);
	void (*tablet_tool_tip)(struct wmiiv_seat *seat, struct wmiiv_tablet_tool *tool,
			uint32_t time_msec, enum wlr_tablet_tool_tip_state state);
	void (*end)(struct wmiiv_seat *seat);
	void (*unref)(struct wmiiv_seat *seat, struct wmiiv_container *container);
	void (*render)(struct wmiiv_seat *seat, struct wmiiv_output *output,
			pixman_region32_t *damage);
	bool allow_set_cursor;
};

struct wmiiv_seat_device {
	struct wmiiv_seat *wmiiv_seat;
	struct wmiiv_input_device *input_device;
	struct wmiiv_keyboard *keyboard;
	struct wmiiv_switch *switch_device;
	struct wmiiv_tablet *tablet;
	struct wmiiv_tablet_pad *tablet_pad;
	struct wl_list link; // wmiiv_seat::devices
};

struct wmiiv_seat_window {
	struct wmiiv_seat *seat;
	struct wmiiv_container *window;

	struct wl_list link; // wmiiv_seat::active_window_stack

	struct wl_listener destroy;
};

struct wmiiv_drag_icon {
	struct wmiiv_seat *seat;
	struct wlr_drag_icon *wlr_drag_icon;
	struct wl_list link; // wmiiv_root::drag_icons

	double x, y; // in layout-local coordinates

	struct wl_listener surface_commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
};

struct wmiiv_drag {
	struct wmiiv_seat *seat;
	struct wlr_drag *wlr_drag;
	struct wl_listener destroy;
};

struct wmiiv_seat {
	struct wlr_seat *wlr_seat;
	struct wmiiv_cursor *cursor;

	// True if a window, in particular the window at the top of the active
	// window stack, has focus.
	bool has_focus;

	// List of windows in focus order.
	struct wl_list active_window_stack;

	struct wmiiv_workspace *workspace;
	char *prev_workspace_name; // for workspace back_and_forth

	// If the focused layer is set, views cannot receive keyboard focus
	struct wlr_layer_surface_v1 *focused_layer;

	// If exclusive_client is set, no other clients will receive input events
	struct wl_client *exclusive_client;

	// Last touch point
	int32_t touch_id;
	double touch_x, touch_y;

	// Seat operations (drag and resize)
	const struct wmiiv_seatop_impl *seatop_impl;
	void *seatop_data;

	uint32_t last_button_serial;

	uint32_t idle_inhibit_sources, idle_wake_sources;

	list_t *deferred_bindings; // struct wmiiv_binding

	struct wmiiv_input_method_relay im_relay;

	struct wl_listener new_node;
	struct wl_listener workspace_destroy;
	struct wl_listener request_start_drag;
	struct wl_listener start_drag;
	struct wl_listener request_set_selection;
	struct wl_listener request_set_primary_selection;

	struct wl_list devices; // wmiiv_seat_device::link
	struct wl_list keyboard_groups; // wmiiv_keyboard_group::link
	struct wl_list keyboard_shortcuts_inhibitors;
				// wmiiv_keyboard_shortcuts_inhibitor::link

	struct wl_list link; // input_manager::seats
};

struct wmiiv_pointer_constraint {
	struct wmiiv_cursor *cursor;
	struct wlr_pointer_constraint_v1 *constraint;

	struct wl_listener set_region;
	struct wl_listener destroy;
};

struct wmiiv_keyboard_shortcuts_inhibitor {
	struct wlr_keyboard_shortcuts_inhibitor_v1 *inhibitor;

	struct wl_listener destroy;

	struct wl_list link; // wmiiv_seat::keyboard_shortcuts_inhibitors
};

struct wmiiv_seat *seat_create(const char *seat_name);

void seat_destroy(struct wmiiv_seat *seat);

void seat_add_device(struct wmiiv_seat *seat,
		struct wmiiv_input_device *device);

void seat_configure_device(struct wmiiv_seat *seat,
		struct wmiiv_input_device *device);

void seat_reset_device(struct wmiiv_seat *seat,
		struct wmiiv_input_device *input_device);

void seat_remove_device(struct wmiiv_seat *seat,
		struct wmiiv_input_device *device);

void seat_configure_xcursor(struct wmiiv_seat *seat);

// TODO (wmiiv) deprecated.
void seat_set_focus(struct wmiiv_seat *seat, struct wmiiv_node *node);

void seat_clear_focus(struct wmiiv_seat *seat);

void seat_set_focus_window(struct wmiiv_seat *seat, struct wmiiv_container *window);

// TODO (wmiiv) deprecated.
void seat_set_focus_workspace(struct wmiiv_seat *seat,
		struct wmiiv_workspace *workspace);

/**
 * Manipulate the focus stack without triggering any other behaviour.
 *
 * This can be used to set focus_inactive by calling the function a second time
 * with the real focus.
 */
// TODO (wmiiv) deprecated.  Should be replaced with window-only equivalent.
void seat_set_raw_focus(struct wmiiv_seat *seat, struct wmiiv_node *node);

void seat_set_focus_surface(struct wmiiv_seat *seat,
		struct wlr_surface *surface, bool unfocus);

void seat_set_focus_layer(struct wmiiv_seat *seat,
		struct wlr_layer_surface_v1 *layer);

void seat_set_exclusive_client(struct wmiiv_seat *seat,
		struct wl_client *client);

struct wmiiv_node *seat_get_focus(struct wmiiv_seat *seat);

struct wmiiv_workspace *seat_get_focused_workspace(struct wmiiv_seat *seat);

// If a scratchpad container is fullscreen global, this can be used to try to
// determine the last focused workspace. Otherwise, this should yield the same
// results as seat_get_focused_workspace.
struct wmiiv_workspace *seat_get_last_known_workspace(struct wmiiv_seat *seat);

struct wmiiv_container *seat_get_focused_container(struct wmiiv_seat *seat);

// Force focus to a particular surface that is not part of the workspace
// hierarchy (used for lockscreen)
void wmiiv_force_focus(struct wlr_surface *surface);

/**
 * Return the last container to be focused for the seat (or the most recently
 * opened if no container has received focused) that is a child of the given
 * container. The focus-inactive container of the root window is the focused
 * container for the seat (if the seat does have focus). This function can be
 * used to determine what container gets focused next if the focused container
 * is destroyed, or focus moves to a container with children and we need to
 * descend into the next leaf in focus order.
 */
struct wmiiv_node *seat_get_focus_inactive(struct wmiiv_seat *seat,
		struct wmiiv_node *node);

struct wmiiv_container *seat_get_focus_inactive_tiling(struct wmiiv_seat *seat,
		struct wmiiv_workspace *workspace);

/**
 * Descend into the focus stack to find the focus-inactive view. Useful for
 * container placement when they change position in the tree.
 */
struct wmiiv_container *seat_get_focus_inactive_view(struct wmiiv_seat *seat,
		struct wmiiv_node *ancestor);

/**
 * Return the immediate child of container which was most recently focused.
 */
struct wmiiv_node *seat_get_active_tiling_child(struct wmiiv_seat *seat,
		struct wmiiv_node *parent);

/**
 * Iterate over the focus-inactive children of the container calling the
 * function on each.
 */
void seat_for_each_node(struct wmiiv_seat *seat,
		void (*f)(struct wmiiv_node *node, void *data), void *data);

void seat_for_each_window(struct wmiiv_seat *seat,
		void (*f)(struct wmiiv_container *window, void *data), void *data);

void seat_apply_config(struct wmiiv_seat *seat, struct seat_config *seat_config);

struct seat_config *seat_get_config(struct wmiiv_seat *seat);

struct seat_config *seat_get_config_by_name(const char *name);

void seat_idle_notify_activity(struct wmiiv_seat *seat,
		enum wmiiv_input_idle_source source);

bool seat_is_input_allowed(struct wmiiv_seat *seat, struct wlr_surface *surface);

void drag_icon_update_position(struct wmiiv_drag_icon *icon);

enum wlr_edges find_resize_edge(struct wmiiv_container *cont,
		struct wlr_surface *surface, struct wmiiv_cursor *cursor);

void seatop_begin_default(struct wmiiv_seat *seat);

void seatop_begin_down(struct wmiiv_seat *seat, struct wmiiv_container *container,
		uint32_t time_msec, double sx, double sy);

void seatop_begin_down_on_surface(struct wmiiv_seat *seat,
		struct wlr_surface *surface, uint32_t time_msec, double sx, double sy);

void seatop_begin_move_floating(struct wmiiv_seat *seat,
		struct wmiiv_container *container);

void seatop_begin_move_tiling_threshold(struct wmiiv_seat *seat,
		struct wmiiv_container *container);

void seatop_begin_move_tiling(struct wmiiv_seat *seat,
		struct wmiiv_container *container);

void seatop_begin_resize_floating(struct wmiiv_seat *seat,
		struct wmiiv_container *container, enum wlr_edges edge);

void seatop_begin_resize_tiling(struct wmiiv_seat *seat,
		struct wmiiv_container *container, enum wlr_edges edge);

struct wmiiv_container *seat_get_focus_inactive_floating(struct wmiiv_seat *seat,
		struct wmiiv_workspace *workspace);

void seat_pointer_notify_button(struct wmiiv_seat *seat, uint32_t time_msec,
		uint32_t button, enum wlr_button_state state);

void seat_consider_warp_to_focus(struct wmiiv_seat *seat);

void seatop_button(struct wmiiv_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state);

void seatop_pointer_motion(struct wmiiv_seat *seat, uint32_t time_msec);

void seatop_pointer_axis(struct wmiiv_seat *seat,
		struct wlr_pointer_axis_event *event);

void seatop_tablet_tool_tip(struct wmiiv_seat *seat,
		struct wmiiv_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state);

void seatop_tablet_tool_motion(struct wmiiv_seat *seat,
		struct wmiiv_tablet_tool *tool, uint32_t time_msec);

void seatop_rebase(struct wmiiv_seat *seat, uint32_t time_msec);

/**
 * End a seatop (ie. free any seatop specific resources).
 */
void seatop_end(struct wmiiv_seat *seat);

/**
 * Instructs the seatop implementation to drop any references to the given
 * container (eg. because the container is destroying).
 * The seatop may choose to abort itself in response to this.
 */
void seatop_unref(struct wmiiv_seat *seat, struct wmiiv_container *container);

/**
 * Instructs a seatop to render anything that it needs to render
 * (eg. dropzone for move-tiling)
 */
void seatop_render(struct wmiiv_seat *seat, struct wmiiv_output *output,
		pixman_region32_t *damage);

bool seatop_allows_set_cursor(struct wmiiv_seat *seat);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the given surface
 * or NULL if none exists.
 */
struct wmiiv_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(const struct wmiiv_seat *seat,
		const struct wlr_surface *surface);

/**
 * Returns the keyboard shortcuts inhibitor that applies to the currently
 * focused surface of a seat or NULL if none exists.
 */
struct wmiiv_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(const struct wmiiv_seat *seat);

#endif
