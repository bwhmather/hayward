#ifndef _WMIIV_VIEW_H
#define _WMIIV_VIEW_H
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include "config.h"
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"

struct wmiiv_container;
struct wmiiv_xdg_decoration;

enum wmiiv_view_type {
	WMIIV_VIEW_XDG_SHELL,
#if HAVE_XWAYLAND
	WMIIV_VIEW_XWAYLAND,
#endif
};

enum wmiiv_view_prop {
	VIEW_PROP_TITLE,
	VIEW_PROP_APP_ID,
	VIEW_PROP_CLASS,
	VIEW_PROP_INSTANCE,
	VIEW_PROP_WINDOW_TYPE,
	VIEW_PROP_WINDOW_ROLE,
#if HAVE_XWAYLAND
	VIEW_PROP_X11_WINDOW_ID,
	VIEW_PROP_X11_PARENT_ID,
#endif
};

struct wmiiv_view_impl {
	void (*get_constraints)(struct wmiiv_view *view, double *min_width,
			double *max_width, double *min_height, double *max_height);
	const char *(*get_string_prop)(struct wmiiv_view *view,
			enum wmiiv_view_prop prop);
	uint32_t (*get_int_prop)(struct wmiiv_view *view, enum wmiiv_view_prop prop);
	uint32_t (*configure)(struct wmiiv_view *view, double lx, double ly,
			int width, int height);
	void (*set_activated)(struct wmiiv_view *view, bool activated);
	void (*set_tiled)(struct wmiiv_view *view, bool tiled);
	void (*set_fullscreen)(struct wmiiv_view *view, bool fullscreen);
	void (*set_resizing)(struct wmiiv_view *view, bool resizing);
	bool (*wants_floating)(struct wmiiv_view *view);
	void (*for_each_surface)(struct wmiiv_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data);
	void (*for_each_popup_surface)(struct wmiiv_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data);
	bool (*is_transient_for)(struct wmiiv_view *child,
			struct wmiiv_view *ancestor);
	void (*close)(struct wmiiv_view *view);
	void (*close_popups)(struct wmiiv_view *view);
	void (*destroy)(struct wmiiv_view *view);
};

struct wmiiv_saved_buffer {
	struct wlr_client_buffer *buffer;
	int x, y;
	int width, height;
	enum wl_output_transform transform;
	struct wlr_fbox source_box;
	struct wl_list link; // wmiiv_view::saved_buffers
};

struct wmiiv_view {
	enum wmiiv_view_type type;
	const struct wmiiv_view_impl *impl;

	struct wmiiv_container *container; // NULL if unmapped and transactions finished
	struct wlr_surface *surface; // NULL for unmapped views
	struct wmiiv_xdg_decoration *xdg_decoration;

	pid_t pid;

	// The size the view would want to be if it weren't tiled.
	// Used when changing a view from tiled to floating.
	int natural_width, natural_height;

	char *title_format;

	bool using_csd;

	struct timespec urgent;
	bool allow_request_urgent;
	struct wl_event_source *urgent_timer;

	struct wl_list saved_buffers; // wmiiv_saved_buffer::link

	// The geometry for whatever the client is committing, regardless of
	// transaction state. Updated on every commit.
	struct wlr_box geometry;

	// The "old" geometry during a transaction. Used to damage the old location
	// when a transaction is applied.
	struct wlr_box saved_geometry;

	struct wlr_foreign_toplevel_handle_v1 *foreign_toplevel;
	struct wl_listener foreign_activate_request;
	struct wl_listener foreign_fullscreen_request;
	struct wl_listener foreign_close_request;
	struct wl_listener foreign_destroy;

	bool destroying;

	list_t *executed_criteria; // struct criteria *

	union {
		struct wlr_xdg_toplevel *wlr_xdg_toplevel;
#if HAVE_XWAYLAND
		struct wlr_xwayland_surface *wlr_xwayland_surface;
#endif
	};

	struct {
		struct wl_signal unmap;
	} events;

	struct wl_listener surface_new_subsurface;

	int max_render_time; // In milliseconds

	enum seat_config_shortcuts_inhibit shortcuts_inhibit;
};

struct wmiiv_xdg_shell_view {
	struct wmiiv_view view;

	struct wl_listener commit;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_fullscreen;
	struct wl_listener set_title;
	struct wl_listener set_app_id;
	struct wl_listener new_popup;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
};
#if HAVE_XWAYLAND
struct wmiiv_xwayland_view {
	struct wmiiv_view view;

	struct wl_listener commit;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_minimize;
	struct wl_listener request_configure;
	struct wl_listener request_fullscreen;
	struct wl_listener request_activate;
	struct wl_listener set_title;
	struct wl_listener set_class;
	struct wl_listener set_role;
	struct wl_listener set_window_type;
	struct wl_listener set_hints;
	struct wl_listener set_decorations;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener override_redirect;
};

struct wmiiv_xwayland_unmanaged {
	struct wlr_xwayland_surface *wlr_xwayland_surface;
	struct wl_list link;

	int lx, ly;

	struct wl_listener request_activate;
	struct wl_listener request_configure;
	struct wl_listener request_fullscreen;
	struct wl_listener commit;
	struct wl_listener set_geometry;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener override_redirect;
};
#endif
struct wmiiv_view_child;

struct wmiiv_view_child_impl {
	void (*get_view_coords)(struct wmiiv_view_child *child, int *sx, int *sy);
	void (*destroy)(struct wmiiv_view_child *child);
};

/**
 * A view child is a surface in the view tree, such as a subsurface or a popup.
 */
struct wmiiv_view_child {
	const struct wmiiv_view_child_impl *impl;
	struct wl_list link;

	struct wmiiv_view *view;
	struct wmiiv_view_child *parent;
	struct wl_list children; // wmiiv_view_child::link
	struct wlr_surface *surface;
	bool mapped;

	struct wl_listener surface_commit;
	struct wl_listener surface_new_subsurface;
	struct wl_listener surface_map;
	struct wl_listener surface_unmap;
	struct wl_listener surface_destroy;
	struct wl_listener view_unmap;
};

struct wmiiv_subsurface {
	struct wmiiv_view_child child;

	struct wl_listener destroy;
};

struct wmiiv_xdg_popup {
	struct wmiiv_view_child child;

	struct wlr_xdg_popup *wlr_xdg_popup;

	struct wl_listener new_popup;
	struct wl_listener destroy;
};

const char *view_get_title(struct wmiiv_view *view);

const char *view_get_app_id(struct wmiiv_view *view);

const char *view_get_class(struct wmiiv_view *view);

const char *view_get_instance(struct wmiiv_view *view);

uint32_t view_get_x11_window_id(struct wmiiv_view *view);

uint32_t view_get_x11_parent_id(struct wmiiv_view *view);

const char *view_get_window_role(struct wmiiv_view *view);

uint32_t view_get_window_type(struct wmiiv_view *view);

const char *view_get_shell(struct wmiiv_view *view);

void view_get_constraints(struct wmiiv_view *view, double *min_width,
		double *max_width, double *min_height, double *max_height);

uint32_t view_configure(struct wmiiv_view *view, double lx, double ly, int width,
	int height);

bool view_inhibit_idle(struct wmiiv_view *view);

/**
 * Whether or not this view's most distant ancestor (possibly itself) is the
 * only visible node in its tree. If the view is tiling, there may be floating
 * views. If the view is floating, there may be tiling views or views in a
 * different floating container.
 */
bool view_ancestor_is_only_visible(struct wmiiv_view *view);

/**
 * Configure the view's position and size based on the container's position and
 * size, taking borders into consideration.
 */
void view_autoconfigure(struct wmiiv_view *view);

void view_set_activated(struct wmiiv_view *view, bool activated);

/**
 * Called when the view requests to be focused.
 */
void view_request_activate(struct wmiiv_view *view);

/**
 * If possible, instructs the client to change their decoration mode.
 */
void view_set_csd_from_server(struct wmiiv_view *view, bool enabled);

/**
 * Updates the view's border setting when the client unexpectedly changes their
 * decoration mode.
 */
void view_update_csd_from_client(struct wmiiv_view *view, bool enabled);

void view_set_tiled(struct wmiiv_view *view, bool tiled);

void view_close(struct wmiiv_view *view);

void view_close_popups(struct wmiiv_view *view);

void view_damage_from(struct wmiiv_view *view);

/**
 * Iterate all surfaces of a view (toplevels + popups).
 */
void view_for_each_surface(struct wmiiv_view *view,
	wlr_surface_iterator_func_t iterator, void *user_data);

/**
 * Iterate all popup surfaces of a view.
 */
void view_for_each_popup_surface(struct wmiiv_view *view,
	wlr_surface_iterator_func_t iterator, void *user_data);

// view implementation

void view_init(struct wmiiv_view *view, enum wmiiv_view_type type,
	const struct wmiiv_view_impl *impl);

void view_destroy(struct wmiiv_view *view);

void view_begin_destroy(struct wmiiv_view *view);

/**
 * Map a view, ie. make it visible in the tree.
 *
 * `fullscreen` should be set to true (and optionally `fullscreen_output`
 * should be populated) if the view should be made fullscreen immediately.
 *
 * `decoration` should be set to true if the client prefers CSD. The client's
 * preference may be ignored.
 */
void view_map(struct wmiiv_view *view, struct wlr_surface *wlr_surface,
	bool fullscreen, struct wlr_output *fullscreen_output, bool decoration);

void view_unmap(struct wmiiv_view *view);

void view_update_size(struct wmiiv_view *view);
void view_center_surface(struct wmiiv_view *view);

void view_child_init(struct wmiiv_view_child *child,
	const struct wmiiv_view_child_impl *impl, struct wmiiv_view *view,
	struct wlr_surface *surface);

void view_child_destroy(struct wmiiv_view_child *child);


struct wmiiv_view *view_from_wlr_xdg_surface(
	struct wlr_xdg_surface *xdg_surface);
#if HAVE_XWAYLAND
struct wmiiv_view *view_from_wlr_xwayland_surface(
	struct wlr_xwayland_surface *xsurface);
#endif
struct wmiiv_view *view_from_wlr_surface(struct wlr_surface *surface);

/**
 * Re-read the view's title property and update any relevant title bars.
 * The force argument makes it recreate the title bars even if the title hasn't
 * changed.
 */
void view_update_title(struct wmiiv_view *view, bool force);

/**
 * Run any criteria that match the view and haven't been run on this view
 * before.
 */
void view_execute_criteria(struct wmiiv_view *view);

/**
 * Returns true if there's a possibility the view may be rendered on screen.
 * Intended for damage tracking.
 */
bool view_is_visible(struct wmiiv_view *view);

void view_set_urgent(struct wmiiv_view *view, bool enable);

bool view_is_urgent(struct wmiiv_view *view);

void view_remove_saved_buffer(struct wmiiv_view *view);

void view_save_buffer(struct wmiiv_view *view);

bool view_is_transient_for(struct wmiiv_view *child, struct wmiiv_view *ancestor);

#endif
