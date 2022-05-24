#ifndef _WMIIV_INPUT_TABLET_H
#define _WMIIV_INPUT_TABLET_H
#include <wlr/types/wlr_layer_shell_v1.h>

struct wmiiv_seat;
struct wlr_tablet_tool;

struct wmiiv_tablet {
	struct wl_list link;
	struct wmiiv_seat_device *seat_device;
	struct wlr_tablet_v2_tablet *tablet_v2;
};

enum wmiiv_tablet_tool_mode {
	WMIIV_TABLET_TOOL_MODE_ABSOLUTE,
	WMIIV_TABLET_TOOL_MODE_RELATIVE,
};

struct wmiiv_tablet_tool {
	struct wmiiv_seat *seat;
	struct wmiiv_tablet *tablet;
	struct wlr_tablet_v2_tablet_tool *tablet_v2_tool;

	enum wmiiv_tablet_tool_mode mode;
	double tilt_x, tilt_y;

	struct wl_listener set_cursor;
	struct wl_listener tool_destroy;
};

struct wmiiv_tablet_pad {
	struct wl_list link;
	struct wmiiv_seat_device *seat_device;
	struct wmiiv_tablet *tablet;
	struct wlr_tablet_v2_tablet_pad *tablet_v2_pad;

	struct wl_listener attach;
	struct wl_listener button;
	struct wl_listener ring;
	struct wl_listener strip;

	struct wlr_surface *current_surface;
	struct wl_listener surface_destroy;

	struct wl_listener tablet_destroy;
};

struct wmiiv_tablet *wmiiv_tablet_create(struct wmiiv_seat *seat,
		struct wmiiv_seat_device *device);

void wmiiv_configure_tablet(struct wmiiv_tablet *tablet);

void wmiiv_tablet_destroy(struct wmiiv_tablet *tablet);

void wmiiv_tablet_tool_configure(struct wmiiv_tablet *tablet,
		struct wlr_tablet_tool *wlr_tool);

struct wmiiv_tablet_pad *wmiiv_tablet_pad_create(struct wmiiv_seat *seat,
		struct wmiiv_seat_device *device);

void wmiiv_configure_tablet_pad(struct wmiiv_tablet_pad *tablet_pad);

void wmiiv_tablet_pad_destroy(struct wmiiv_tablet_pad *tablet_pad);

void wmiiv_tablet_pad_notify_enter(struct wmiiv_tablet_pad *tablet_pad,
		struct wlr_surface *surface);

#endif
