#ifndef _HAYWARDBAR_BAR_H
#define _HAYWARDBAR_BAR_H
#include <wayland-client.h>

#include "hayward-client/pool-buffer.h"

#include "config.h"
#include "input.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

struct haywardbar_config;
struct haywardbar_output;
#if HAVE_TRAY
struct haywardbar_tray;
#endif
struct haywardbar_workspace;
struct loop;

struct haywardbar {
	char *id;
	char *mode;
	bool mode_pango_markup;

	// only relevant when bar is in "hide" mode
	bool visible_by_modifier;
	bool visible_by_urgency;
	bool visible_by_mode;
	bool visible;

	struct wl_display *display;
	struct wl_compositor *compositor;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zxdg_output_manager_v1 *xdg_output_manager;
	struct wl_shm *shm;

	struct haywardbar_config *config;
	struct status_line *status;

	struct loop *eventloop;

	int ipc_event_socketfd;
	int ipc_socketfd;

	struct wl_list outputs;		   // haywardbar_output::link
	struct wl_list unused_outputs; // haywardbar_output::link
	struct wl_list seats;		   // haywardbar_seat::link

#if HAVE_TRAY
	struct haywardbar_tray *tray;
#endif

	bool running;
};

struct haywardbar_output {
	struct wl_list link; // haywardbar::outputs
	struct haywardbar *bar;
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	uint32_t wl_name;

	struct wl_list workspaces; // haywardbar_workspace::link
	struct wl_list hotspots;   // haywardbar_hotspot::link

	char *name;
	char *identifier;
	bool focused;

	uint32_t width, height;
	int32_t scale;
	enum wl_output_subpixel subpixel;
	struct pool_buffer buffers[2];
	struct pool_buffer *current_buffer;
	bool dirty;
	bool frame_scheduled;

	uint32_t output_height, output_width, output_x, output_y;
};

struct haywardbar_workspace {
	struct wl_list link; // haywardbar_output::workspaces
	int num;
	char *name;
	char *label;
	bool focused;
	bool visible;
	bool urgent;
};

bool bar_setup(struct haywardbar *bar, const char *socket_path);
void bar_run(struct haywardbar *bar);
void bar_teardown(struct haywardbar *bar);

void set_bar_dirty(struct haywardbar *bar);

/*
 * Determines whether the bar should be visible and changes it to be so.
 * If the current visibility of the bar is the different to what it should be,
 * then it adds or destroys the layer surface as required,
 * as well as sending the cont or stop signal to the status command.
 * If the current visibility of the bar is already what it should be,
 * then this function is a no-op, unless moving_layer is true, which occurs
 * when the bar changes from "hide" to "dock" mode or vice versa, and the bar
 * needs to be destroyed and re-added in order to change its layer.
 *
 * Returns true if the bar is now visible, otherwise false.
 */
bool determine_bar_visibility(struct haywardbar *bar, bool moving_layer);
void free_workspaces(struct wl_list *list);

void status_in(int fd, short mask, void *data);

void destroy_layer_surface(struct haywardbar_output *output);

#endif
