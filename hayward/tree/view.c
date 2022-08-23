#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include "config.h"
#if HAVE_XWAYLAND
#include <wlr/xwayland.h>
#endif
#include "list.h"
#include "log.h"
#include "hayward/commands.h"
#include "hayward/desktop.h"
#include "hayward/desktop/transaction.h"
#include "hayward/desktop/idle_inhibit_v1.h"
#include "hayward/input/cursor.h"
#include "hayward/ipc-server.h"
#include "hayward/output.h"
#include "hayward/input/seat.h"
#include "hayward/server.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/column.h"
#include "hayward/tree/window.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"
#include "hayward/config.h"
#include "hayward/xdg_decoration.h"
#include "pango.h"
#include "stringop.h"

void view_init(struct hayward_view *view, enum hayward_view_type type,
		const struct hayward_view_impl *impl) {
	view->type = type;
	view->impl = impl;
	wl_list_init(&view->saved_buffers);
	view->allow_request_urgent = true;
	view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DEFAULT;
	wl_signal_init(&view->events.unmap);
}

void view_destroy(struct hayward_view *view) {
	hayward_assert(view->surface == NULL, "Tried to free mapped view");
	hayward_assert(view->destroying,
				"Tried to free view which wasn't marked as destroying");
	hayward_assert(view->window == NULL,
				"Tried to free view which still has a container "
				"(might have a pending transaction?)");
	wl_list_remove(&view->events.unmap.listener_list);
	if (!wl_list_empty(&view->saved_buffers)) {
		view_remove_saved_buffer(view);
	}

	free(view->title_format);

	if (view->impl->destroy) {
		view->impl->destroy(view);
	} else {
		free(view);
	}
}

void view_begin_destroy(struct hayward_view *view) {
	hayward_assert(view->surface == NULL, "Tried to destroy a mapped view");
	view->destroying = true;

	if (!view->window) {
		view_destroy(view);
	}
}

const char *view_get_title(struct hayward_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_TITLE);
	}
	return NULL;
}

const char *view_get_app_id(struct hayward_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_APP_ID);
	}
	return NULL;
}

const char *view_get_class(struct hayward_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_CLASS);
	}
	return NULL;
}

const char *view_get_instance(struct hayward_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_INSTANCE);
	}
	return NULL;
}
#if HAVE_XWAYLAND
uint32_t view_get_x11_window_id(struct hayward_view *view) {
	if (view->impl->get_int_prop) {
		return view->impl->get_int_prop(view, VIEW_PROP_X11_WINDOW_ID);
	}
	return 0;
}

uint32_t view_get_x11_parent_id(struct hayward_view *view) {
	if (view->impl->get_int_prop) {
		return view->impl->get_int_prop(view, VIEW_PROP_X11_PARENT_ID);
	}
	return 0;
}
#endif
const char *view_get_window_role(struct hayward_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_WINDOW_ROLE);
	}
	return NULL;
}

uint32_t view_get_window_type(struct hayward_view *view) {
	if (view->impl->get_int_prop) {
		return view->impl->get_int_prop(view, VIEW_PROP_WINDOW_TYPE);
	}
	return 0;
}

const char *view_get_shell(struct hayward_view *view) {
	switch(view->type) {
	case HAYWARD_VIEW_XDG_SHELL:
		return "xdg_shell";
#if HAVE_XWAYLAND
	case HAYWARD_VIEW_XWAYLAND:
		return "xwayland";
#endif
	}
	return "unknown";
}

void view_get_constraints(struct hayward_view *view, double *min_width,
		double *max_width, double *min_height, double *max_height) {
	if (view->impl->get_constraints) {
		view->impl->get_constraints(view,
				min_width, max_width, min_height, max_height);
	} else {
		*min_width = DBL_MIN;
		*max_width = DBL_MAX;
		*min_height = DBL_MIN;
		*max_height = DBL_MAX;
	}
}

uint32_t view_configure(struct hayward_view *view, double lx, double ly, int width,
		int height) {
	if (view->impl->configure) {
		return view->impl->configure(view, lx, ly, width, height);
	}
	return 0;
}

bool view_inhibit_idle(struct hayward_view *view) {
	struct hayward_idle_inhibitor_v1 *user_inhibitor =
		hayward_idle_inhibit_v1_user_inhibitor_for_view(view);

	struct hayward_idle_inhibitor_v1 *application_inhibitor =
		hayward_idle_inhibit_v1_application_inhibitor_for_view(view);

	if (!user_inhibitor && !application_inhibitor) {
		return false;
	}

	if (!user_inhibitor) {
		return hayward_idle_inhibit_v1_is_active(application_inhibitor);
	}

	if (!application_inhibitor) {
		return hayward_idle_inhibit_v1_is_active(user_inhibitor);
	}

	return hayward_idle_inhibit_v1_is_active(user_inhibitor)
		|| hayward_idle_inhibit_v1_is_active(application_inhibitor);
}

bool view_ancestor_is_only_visible(struct hayward_view *view) {
	struct hayward_window *window = view->window;
	struct hayward_column *column = window->pending.parent;

	list_t *siblings = column_get_siblings(column);
	if (siblings && siblings->length > 1) {
		return false;
	}

	return true;
}

static bool view_is_only_visible(struct hayward_view *view) {
	struct hayward_window *window = view->window;

	enum hayward_column_layout layout = window_parent_layout(window);
	if (layout != L_STACKED) {
		list_t *siblings = window_get_siblings(window);
		if (siblings && siblings->length > 1) {
			return false;
		}
	}

	struct hayward_column *column = window->pending.parent;
	list_t *siblings = column_get_siblings(column);
	if (siblings && siblings->length > 1) {
		return false;
	}

	return true;
}

static bool gaps_to_edge(struct hayward_view *view) {
	struct side_gaps gaps = view->window->pending.workspace->current_gaps;
	return gaps.top > 0 || gaps.right > 0 || gaps.bottom > 0 || gaps.left > 0;
}

void view_autoconfigure(struct hayward_view *view) {
	struct hayward_window *window = view->window;
	struct hayward_workspace *workspace = window->pending.workspace;

	struct hayward_output *output = workspace_get_active_output(workspace);

	if (window->pending.fullscreen) {
		window->pending.content_x = output->lx;
		window->pending.content_y = output->ly;
		window->pending.content_width = output->width;
		window->pending.content_height = output->height;
		return;
	}

	window->pending.border_top = window->pending.border_bottom = true;
	window->pending.border_left = window->pending.border_right = true;
	double y_offset = 0;
	double y_allocation = window->pending.height;

	if (!window_is_floating(window) && workspace) {
		if (config->hide_edge_borders == E_BOTH
				|| config->hide_edge_borders == E_VERTICAL) {
			window->pending.border_left = window->pending.x != workspace->pending.x;
			int right_x = window->pending.x + window->pending.width;
			window->pending.border_right = right_x != workspace->pending.x + workspace->pending.width;
		}

		if (config->hide_edge_borders == E_BOTH
				|| config->hide_edge_borders == E_HORIZONTAL) {
			window->pending.border_top = window->pending.y != workspace->pending.y;
			int bottom_y = window->pending.y + window->pending.height;
			window->pending.border_bottom = bottom_y != workspace->pending.y + workspace->pending.height;
		}

		bool smart = config->hide_edge_borders_smart == ESMART_ON ||
			(config->hide_edge_borders_smart == ESMART_NO_GAPS &&
			!gaps_to_edge(view));
		if (smart) {
			bool show_border = !view_is_only_visible(view);
			window->pending.border_left &= show_border;
			window->pending.border_right &= show_border;
			window->pending.border_top &= show_border;
			window->pending.border_bottom &= show_border;
		}
	}

	if (!window_is_floating(window)) {
		// In a stacked container, the container's y is the top of the
		// title area. We have to offset the surface y by the height of
		// the title, bar, and disable any top border because we'll
		// always have the title bar.
		list_t *siblings = window_get_siblings(window);
		enum hayward_column_layout layout = window_parent_layout(window);
		if (layout == L_STACKED) {
			y_offset = window_titlebar_height() * (1 + list_find(siblings, window));
			y_allocation -= window_titlebar_height() * siblings->length;
			window->pending.border_top = false;
		}
	}

	double x, y, width, height;
	switch (window->pending.border) {
	default:
	case B_CSD:
	case B_NONE:
		x = window->pending.x;
		y = window->pending.y + y_offset;
		width = window->pending.width;
		height = y_allocation;
		break;
	case B_PIXEL:
		x = window->pending.x + window->pending.border_thickness * window->pending.border_left;
		y = window->pending.y + window->pending.border_thickness * window->pending.border_top + y_offset;
		width = window->pending.width
			- window->pending.border_thickness * window->pending.border_left
			- window->pending.border_thickness * window->pending.border_right;
		height = y_allocation
			- window->pending.border_thickness * window->pending.border_top
			- window->pending.border_thickness * window->pending.border_bottom;
		break;
	case B_NORMAL:
		// Height is: 1px border + 3px pad + title height + 3px pad + 1px border
		x = window->pending.x + window->pending.border_thickness * window->pending.border_left;
		width = window->pending.width
			- window->pending.border_thickness * window->pending.border_left
			- window->pending.border_thickness * window->pending.border_right;
		if (y_offset) {
			y = window->pending.y + y_offset;
			height = y_allocation
				- window->pending.border_thickness * window->pending.border_bottom;
		} else {
			y = window->pending.y + window_titlebar_height();
			height = window->pending.height - window_titlebar_height()
				- window->pending.border_thickness * window->pending.border_bottom;
		}
		break;
	}

	window->pending.content_x = x;
	window->pending.content_y = y;
	window->pending.content_width = width;
	window->pending.content_height = height;
}

void view_set_activated(struct hayward_view *view, bool activated) {
	if (view->impl->set_activated) {
		view->impl->set_activated(view, activated);
	}
	if (view->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_activated(
				view->foreign_toplevel, activated);
	}
}

void view_request_activate(struct hayward_view *view) {
	struct hayward_workspace *workspace = view->window->pending.workspace;

	switch (config->focus_on_window_activation) {
	case FOWA_SMART:
		if (workspace_is_visible(workspace)) {
			root_set_focused_window(view->window);
		} else {
			view_set_urgent(view, true);
		}
		break;
	case FOWA_URGENT:
		view_set_urgent(view, true);
		break;
	case FOWA_FOCUS:
		root_set_focused_window(view->window);
		break;
	case FOWA_NONE:
		break;
	}
}

void view_set_csd_from_server(struct hayward_view *view, bool enabled) {
	hayward_log(HAYWARD_DEBUG, "Telling view %p to set CSD to %i", (void *) view, enabled);
	if (view->xdg_decoration) {
		uint32_t mode = enabled ?
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE :
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
		wlr_xdg_toplevel_decoration_v1_set_mode(
				view->xdg_decoration->wlr_xdg_decoration, mode);
	}
	view->using_csd = enabled;
}

void view_update_csd_from_client(struct hayward_view *view, bool enabled) {
	hayward_log(HAYWARD_DEBUG, "View %p updated CSD to %i", (void *) view, enabled);
	struct hayward_window *window = view->window;
	if (enabled && window && window->pending.border != B_CSD) {
		window->saved_border = window->pending.border;
		if (window_is_floating(window)) {
			window->pending.border = B_CSD;
		}
	} else if (!enabled && window && window->pending.border == B_CSD) {
		window->pending.border = window->saved_border;
	}
	view->using_csd = enabled;
}

void view_set_tiled(struct hayward_view *view, bool tiled) {
	if (view->impl->set_tiled) {
		view->impl->set_tiled(view, tiled);
	}
}

void view_close(struct hayward_view *view) {
	if (view->impl->close) {
		view->impl->close(view);
	}
}

void view_close_popups(struct hayward_view *view) {
	if (view->impl->close_popups) {
		view->impl->close_popups(view);
	}
}

void view_damage_from(struct hayward_view *view) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		output_damage_from_view(output, view);
	}
}

void view_for_each_surface(struct hayward_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (!view->surface) {
		return;
	}
	if (view->impl->for_each_surface) {
		view->impl->for_each_surface(view, iterator, user_data);
	} else {
		wlr_surface_for_each_surface(view->surface, iterator, user_data);
	}
}

void view_for_each_popup_surface(struct hayward_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (!view->surface) {
		return;
	}
	if (view->impl->for_each_popup_surface) {
		view->impl->for_each_popup_surface(view, iterator, user_data);
	}
}

static void view_subsurface_create(struct hayward_view *view,
	struct wlr_subsurface *subsurface);

static void view_init_subsurfaces(struct hayward_view *view,
	struct wlr_surface *surface);

static void view_child_init_subsurfaces(struct hayward_view_child *view_child,
	struct wlr_surface *surface);

static void view_handle_surface_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct hayward_view *view =
		wl_container_of(listener, view, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;
	view_subsurface_create(view, subsurface);
}

static void view_populate_pid(struct hayward_view *view) {
	pid_t pid;
	switch (view->type) {
#if HAVE_XWAYLAND
	case HAYWARD_VIEW_XWAYLAND:;
		struct wlr_xwayland_surface *surf =
			wlr_xwayland_surface_from_wlr_surface(view->surface);
		pid = surf->pid;
		break;
#endif
	case HAYWARD_VIEW_XDG_SHELL:;
		struct wl_client *client =
			wl_resource_get_client(view->surface->resource);
		wl_client_get_credentials(client, &pid, NULL, NULL);
		break;
	}
	view->pid = pid;
}

static bool should_focus(struct hayward_view *view) {
	struct hayward_workspace *prev_workspace = root_get_active_workspace();
	struct hayward_workspace *map_workspace = view->window->pending.workspace;

	// View opened "under" fullscreen view should not be given focus.
	if (!map_workspace || map_workspace->pending.fullscreen) {
		return false;
	}

	// Views can only take focus if they are mapped into the active workspace
	if (prev_workspace != map_workspace) {
		return false;
	}

	return true;
}

static void handle_foreign_activate_request(struct wl_listener *listener, void *data) {
	struct hayward_view *view = wl_container_of(listener, view, foreign_activate_request);

	root_set_focused_window(view->window);
	window_raise_floating(view->window);

	transaction_commit_dirty();
}

static void handle_foreign_fullscreen_request(
		struct wl_listener *listener, void *data) {
	struct hayward_view *view = wl_container_of(
			listener, view, foreign_fullscreen_request);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

	struct hayward_window *window = view->window;

	if (event->fullscreen && event->output && event->output->data) {
		struct hayward_output *output = event->output->data;
		struct hayward_workspace *workspace = output_get_active_workspace(output);
		if (workspace) {
			window_move_to_workspace(window, workspace);
		}
	}

	window_set_fullscreen(window, event->fullscreen);
	if (event->fullscreen) {
		arrange_root();
	} else {
		if (window->pending.parent) {
			arrange_column(window->pending.parent);
		} else if (window->pending.workspace) {
			arrange_workspace(window->pending.workspace);
		}
	}
	transaction_commit_dirty();
}

static void handle_foreign_close_request(
		struct wl_listener *listener, void *data) {
	struct hayward_view *view = wl_container_of(
			listener, view, foreign_close_request);
	view_close(view);
}

static void handle_foreign_destroy(
		struct wl_listener *listener, void *data) {
	struct hayward_view *view = wl_container_of(
			listener, view, foreign_destroy);

	wl_list_remove(&view->foreign_activate_request.link);
	wl_list_remove(&view->foreign_fullscreen_request.link);
	wl_list_remove(&view->foreign_close_request.link);
	wl_list_remove(&view->foreign_destroy.link);
}

void view_map(struct hayward_view *view, struct wlr_surface *wlr_surface,
			  bool fullscreen, struct wlr_output *fullscreen_output,
			  bool decoration) {
	hayward_assert(view->surface == NULL, "cannot map mapped view");
	view->surface = wlr_surface;
	view_populate_pid(view);
	view->window = window_create(view);

	// If there is a request to be opened fullscreen on a specific output, try
	// to honor that request. Otherwise, fallback to assigns, pid mappings,
	// focused workspace, etc
	struct hayward_workspace *workspace = root_get_active_workspace();
	hayward_assert(workspace != NULL, "Expected workspace");

	struct hayward_output *output = root_get_active_output();
	if (fullscreen_output && fullscreen_output->data) {
		output = fullscreen_output->data;
	}
	hayward_assert(output != NULL, "Expected output");

	view->foreign_toplevel =
		wlr_foreign_toplevel_handle_v1_create(server.foreign_toplevel_manager);
	view->foreign_activate_request.notify = handle_foreign_activate_request;
	wl_signal_add(&view->foreign_toplevel->events.request_activate,
			&view->foreign_activate_request);
	view->foreign_fullscreen_request.notify = handle_foreign_fullscreen_request;
	wl_signal_add(&view->foreign_toplevel->events.request_fullscreen,
			&view->foreign_fullscreen_request);
	view->foreign_close_request.notify = handle_foreign_close_request;
	wl_signal_add(&view->foreign_toplevel->events.request_close,
			&view->foreign_close_request);
	view->foreign_destroy.notify = handle_foreign_destroy;
	wl_signal_add(&view->foreign_toplevel->events.destroy,
			&view->foreign_destroy);

	view_init_subsurfaces(view, wlr_surface);
	wl_signal_add(&wlr_surface->events.new_subsurface,
			&view->surface_new_subsurface);
	view->surface_new_subsurface.notify = view_handle_surface_new_subsurface;

	const char *app_id;
	const char *class;
	if ((app_id = view_get_app_id(view)) != NULL) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id);
	} else if ((class = view_get_class(view)) != NULL) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, class);
	}

	if (view->impl->wants_floating && view->impl->wants_floating(view)) {
		workspace_add_floating(workspace, view->window);

		view->window->pending.border = config->floating_border;
		view->window->pending.border_thickness = config->floating_border_thickness;
		window_set_floating(view->window, true);
	} else {
		struct hayward_window *target_sibling = workspace_get_active_tiling_window(workspace);
		if (target_sibling) {
			column_add_sibling(target_sibling, view->window, 1);
		} else {
			struct hayward_column *column = column_create();
			workspace_insert_tiling(workspace, output, column, 0);
			column_add_child(column, view->window);
		}

		view->window->pending.border = config->border;
		view->window->pending.border_thickness = config->border_thickness;
		view_set_tiled(view, true);

		if (target_sibling) {
			arrange_column(view->window->pending.parent);
		} else {
			arrange_workspace(workspace);
		}
	}

	if (config->popup_during_fullscreen == POPUP_LEAVE &&
			view->window->pending.workspace &&
			view->window->pending.workspace->pending.fullscreen &&
			view->window->pending.workspace->pending.fullscreen->view) {
		struct hayward_window *fs = view->window->pending.workspace->pending.fullscreen;
		if (view_is_transient_for(view, fs->view)) {
			window_set_fullscreen(fs, false);
		}
	}

	if (decoration) {
		view_update_csd_from_client(view, decoration);
	}

	if (fullscreen) {
		// Fullscreen windows still have to have a place as regular
		// tiling or floating windows, so this does not make the
		// previous logic unnecessary.
		window_set_fullscreen(view->window, true);
	}

	view_update_title(view, false);

	bool set_focus = should_focus(view);

#if HAVE_XWAYLAND
	if (wlr_surface_is_xwayland_surface(wlr_surface)) {
		struct wlr_xwayland_surface *xsurface =
				wlr_xwayland_surface_from_wlr_surface(wlr_surface);
		set_focus &= wlr_xwayland_icccm_input_model(xsurface) !=
				WLR_ICCCM_INPUT_MODEL_NONE;
	}
#endif

	if (set_focus) {
		root_set_focused_window(view->window);
	}

	ipc_event_window(view->window, "new");
}

void view_unmap(struct hayward_view *view) {
	wl_signal_emit(&view->events.unmap, view);

	wl_list_remove(&view->surface_new_subsurface.link);

	if (view->urgent_timer) {
		wl_event_source_remove(view->urgent_timer);
		view->urgent_timer = NULL;
	}

	if (view->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_destroy(view->foreign_toplevel);
		view->foreign_toplevel = NULL;
	}

	struct hayward_column *parent = view->window->pending.parent;
	struct hayward_workspace *workspace = view->window->pending.workspace;
	window_begin_destroy(view->window);
	if (parent) {
		column_consider_destroy(parent);
	} else if (workspace) {
		// TODO (hayward) shouldn't be possible.
		workspace_consider_destroy(workspace);
	}

	if (workspace && !workspace->node.destroying) {
		arrange_workspace(workspace);
		workspace_detect_urgent(workspace);
	}

	struct hayward_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat->cursor->image_surface = NULL;
		if (seat->cursor->active_constraint) {
			struct wlr_surface *constrain_surface =
				seat->cursor->active_constraint->surface;
			if (view_from_wlr_surface(constrain_surface) == view) {
				hayward_cursor_constrain(seat->cursor, NULL);
			}
		}
	}

	transaction_commit_dirty();
	view->surface = NULL;
}

void view_update_size(struct hayward_view *view) {
	struct hayward_window *container = view->window;
	container->pending.content_width = view->geometry.width;
	container->pending.content_height = view->geometry.height;
	window_set_geometry_from_content(container);
}

void view_center_surface(struct hayward_view *view) {
	struct hayward_window *container = view->window;
	// We always center the current coordinates rather than the next, as the
	// geometry immediately affects the currently active rendering.
	container->surface_x = fmax(container->current.content_x, container->current.content_x +
			(container->current.content_width - view->geometry.width) / 2);
	container->surface_y = fmax(container->current.content_y, container->current.content_y +
			(container->current.content_height - view->geometry.height) / 2);
}

static const struct hayward_view_child_impl subsurface_impl;

static void subsurface_get_view_coords(struct hayward_view_child *child,
		int *sx, int *sy) {
	struct wlr_surface *surface = child->surface;
	if (child->parent && child->parent->impl &&
			child->parent->impl->get_view_coords) {
		child->parent->impl->get_view_coords(child->parent, sx, sy);
	} else {
		*sx = *sy = 0;
	}
	struct wlr_subsurface *subsurface =
		wlr_subsurface_from_wlr_surface(surface);
	*sx += subsurface->current.x;
	*sy += subsurface->current.y;
}

static void subsurface_destroy(struct hayward_view_child *child) {
	hayward_assert(child->impl == &subsurface_impl, "Expected a subsurface");
	struct hayward_subsurface *subsurface = (struct hayward_subsurface *)child;
	wl_list_remove(&subsurface->destroy.link);
	free(subsurface);
}

static const struct hayward_view_child_impl subsurface_impl = {
	.get_view_coords = subsurface_get_view_coords,
	.destroy = subsurface_destroy,
};

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct hayward_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);
	struct hayward_view_child *child = &subsurface->child;
	view_child_destroy(child);
}

static void view_child_damage(struct hayward_view_child *child, bool whole);

static void view_subsurface_create(struct hayward_view *view,
		struct wlr_subsurface *wlr_subsurface) {
	struct hayward_subsurface *subsurface =
		calloc(1, sizeof(struct hayward_subsurface));
	if (subsurface == NULL) {
		hayward_log(HAYWARD_ERROR, "Allocation failed");
		return;
	}
	view_child_init(&subsurface->child, &subsurface_impl, view,
		wlr_subsurface->surface);

	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->destroy.notify = subsurface_handle_destroy;

	subsurface->child.mapped = true;

	view_child_damage(&subsurface->child, true);
}

static void view_child_subsurface_create(struct hayward_view_child *child,
		struct wlr_subsurface *wlr_subsurface) {
	struct hayward_subsurface *subsurface =
		calloc(1, sizeof(struct hayward_subsurface));
	if (subsurface == NULL) {
		hayward_log(HAYWARD_ERROR, "Allocation failed");
		return;
	}
	subsurface->child.parent = child;
	wl_list_insert(&child->children, &subsurface->child.link);
	view_child_init(&subsurface->child, &subsurface_impl, child->view,
		wlr_subsurface->surface);

	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->destroy.notify = subsurface_handle_destroy;

	subsurface->child.mapped = true;

	view_child_damage(&subsurface->child, true);
}

static bool view_child_is_mapped(struct hayward_view_child *child) {
	while (child) {
		if (!child->mapped) {
			return false;
		}
		child = child->parent;
	}
	return true;
}

static void view_child_damage(struct hayward_view_child *child, bool whole) {
	if (!child || !view_child_is_mapped(child) || !child->view || !child->view->window) {
		return;
	}
	int sx, sy;
	child->impl->get_view_coords(child, &sx, &sy);
	desktop_damage_surface(child->surface,
			child->view->window->pending.content_x -
				child->view->geometry.x + sx,
			child->view->window->pending.content_y -
				child->view->geometry.y + sy, whole);
}

static void view_child_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct hayward_view_child *child =
		wl_container_of(listener, child, surface_commit);
	view_child_damage(child, false);
}

static void view_child_handle_surface_new_subsurface(
		struct wl_listener *listener, void *data) {
	struct hayward_view_child *child =
		wl_container_of(listener, child, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;
	view_child_subsurface_create(child, subsurface);
}

static void view_child_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct hayward_view_child *child =
		wl_container_of(listener, child, surface_destroy);
	view_child_destroy(child);
}

static void view_init_subsurfaces(struct hayward_view *view,
		struct wlr_surface *surface) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->current.subsurfaces_below,
			current.link) {
		view_subsurface_create(view, subsurface);
	}
	wl_list_for_each(subsurface, &surface->current.subsurfaces_above,
			current.link) {
		view_subsurface_create(view, subsurface);
	}
}

static void view_child_init_subsurfaces(struct hayward_view_child *view_child,
		struct wlr_surface *surface) {
	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->current.subsurfaces_below,
			current.link) {
		view_child_subsurface_create(view_child, subsurface);
	}
	wl_list_for_each(subsurface, &surface->current.subsurfaces_above,
			current.link) {
		view_child_subsurface_create(view_child, subsurface);
	}
}

static void view_child_handle_surface_map(struct wl_listener *listener,
		void *data) {
	struct hayward_view_child *child =
		wl_container_of(listener, child, surface_map);
	child->mapped = true;
	view_child_damage(child, true);
}

static void view_child_handle_surface_unmap(struct wl_listener *listener,
		void *data) {
	struct hayward_view_child *child =
		wl_container_of(listener, child, surface_unmap);
	view_child_damage(child, true);
	child->mapped = false;
}

static void view_child_handle_view_unmap(struct wl_listener *listener,
		void *data) {
	struct hayward_view_child *child =
		wl_container_of(listener, child, view_unmap);
	view_child_damage(child, true);
	child->mapped = false;
}

void view_child_init(struct hayward_view_child *child,
		const struct hayward_view_child_impl *impl, struct hayward_view *view,
		struct wlr_surface *surface) {
	child->impl = impl;
	child->view = view;
	child->surface = surface;
	wl_list_init(&child->children);

	wl_signal_add(&surface->events.commit, &child->surface_commit);
	child->surface_commit.notify = view_child_handle_surface_commit;
	wl_signal_add(&surface->events.new_subsurface,
		&child->surface_new_subsurface);
	child->surface_new_subsurface.notify =
		view_child_handle_surface_new_subsurface;
	wl_signal_add(&surface->events.destroy, &child->surface_destroy);
	child->surface_destroy.notify = view_child_handle_surface_destroy;

	// Not all child views have a map/unmap event
	child->surface_map.notify = view_child_handle_surface_map;
	wl_list_init(&child->surface_map.link);
	child->surface_unmap.notify = view_child_handle_surface_unmap;
	wl_list_init(&child->surface_unmap.link);

	wl_signal_add(&view->events.unmap, &child->view_unmap);
	child->view_unmap.notify = view_child_handle_view_unmap;

	struct hayward_window *window = child->view->window;
	if (window != NULL) {
		// TODO view can overlap multiple outputs.
		struct hayward_output *output = window->pending.output;
		if (output != NULL) {
			wlr_surface_send_enter(child->surface, output->wlr_output);
		}
	}

	view_child_init_subsurfaces(child, surface);
}

void view_child_destroy(struct hayward_view_child *child) {
	if (view_child_is_mapped(child) && child->view->window != NULL) {
		view_child_damage(child, true);
	}

	if (child->parent != NULL) {
		wl_list_remove(&child->link);
		child->parent = NULL;
	}

	struct hayward_view_child *subchild, *tmpchild;
	wl_list_for_each_safe(subchild, tmpchild, &child->children, link) {
		wl_list_remove(&subchild->link);
		subchild->parent = NULL;
		// The subchild lost its parent link, so it cannot see that the parent
		// is unmapped. Unmap it directly.
		subchild->mapped = false;
	}

	wl_list_remove(&child->surface_commit.link);
	wl_list_remove(&child->surface_destroy.link);
	wl_list_remove(&child->surface_map.link);
	wl_list_remove(&child->surface_unmap.link);
	wl_list_remove(&child->view_unmap.link);
	wl_list_remove(&child->surface_new_subsurface.link);

	if (child->impl && child->impl->destroy) {
		child->impl->destroy(child);
	} else {
		free(child);
	}
}

struct hayward_view *view_from_wlr_surface(struct wlr_surface *wlr_surface) {
	if (wlr_surface_is_xdg_surface(wlr_surface)) {
		struct wlr_xdg_surface *xdg_surface =
			wlr_xdg_surface_from_wlr_surface(wlr_surface);
		if (xdg_surface == NULL) {
			return NULL;
		}
		return view_from_wlr_xdg_surface(xdg_surface);
	}
#if HAVE_XWAYLAND
	if (wlr_surface_is_xwayland_surface(wlr_surface)) {
		struct wlr_xwayland_surface *xsurface =
			wlr_xwayland_surface_from_wlr_surface(wlr_surface);
		if (xsurface == NULL) {
			return NULL;
		}
		return view_from_wlr_xwayland_surface(xsurface);
	}
#endif
	if (wlr_surface_is_subsurface(wlr_surface)) {
		struct wlr_subsurface *subsurface =
			wlr_subsurface_from_wlr_surface(wlr_surface);
		if (subsurface == NULL) {
			return NULL;
		}
		return view_from_wlr_surface(subsurface->parent);
	}
	if (wlr_surface_is_layer_surface(wlr_surface)) {
		return NULL;
	}

	const char *role = wlr_surface->role ? wlr_surface->role->name : NULL;
	hayward_log(HAYWARD_DEBUG, "Surface of unknown type (role %s): %p",
		role, (void *) wlr_surface);
	return NULL;
}

static char *escape_pango_markup(const char *buffer) {
	size_t length = escape_markup_text(buffer, NULL);
	char *escaped_title = calloc(length + 1, sizeof(char));
	escape_markup_text(buffer, escaped_title);
	return escaped_title;
}

static size_t append_prop(char *buffer, const char *value) {
	if (!value) {
		return 0;
	}
	// If using pango_markup in font, we need to escape all markup chars
	// from values to make sure tags are not inserted by clients
	if (config->pango_markup) {
		char *escaped_value = escape_pango_markup(value);
		lenient_strcat(buffer, escaped_value);
		size_t len = strlen(escaped_value);
		free(escaped_value);
		return len;
	} else {
		lenient_strcat(buffer, value);
		return strlen(value);
	}
}

/**
 * Calculate and return the length of the formatted title.
 * If buffer is not NULL, also populate the buffer with the formatted title.
 */
static size_t parse_title_format(struct hayward_view *view, char *buffer) {
	if (!view->title_format || strcmp(view->title_format, "%title") == 0) {
		return append_prop(buffer, view_get_title(view));
	}

	size_t len = 0;
	char *format = view->title_format;
	char *next = strchr(format, '%');
	while (next) {
		// Copy everything up to the %
		lenient_strncat(buffer, format, next - format);
		len += next - format;
		format = next;

		if (strncmp(next, "%title", 6) == 0) {
			len += append_prop(buffer, view_get_title(view));
			format += 6;
		} else if (strncmp(next, "%app_id", 7) == 0) {
			len += append_prop(buffer, view_get_app_id(view));
			format += 7;
		} else if (strncmp(next, "%class", 6) == 0) {
			len += append_prop(buffer, view_get_class(view));
			format += 6;
		} else if (strncmp(next, "%instance", 9) == 0) {
			len += append_prop(buffer, view_get_instance(view));
			format += 9;
		} else if (strncmp(next, "%shell", 6) == 0) {
			len += append_prop(buffer, view_get_shell(view));
			format += 6;
		} else {
			lenient_strcat(buffer, "%");
			++format;
			++len;
		}
		next = strchr(format, '%');
	}
	lenient_strcat(buffer, format);
	len += strlen(format);

	return len;
}

void view_update_title(struct hayward_view *view, bool force) {
	const char *title = view_get_title(view);

	if (!force) {
		if (title && view->window->title &&
				strcmp(title, view->window->title) == 0) {
			return;
		}
		if (!title && !view->window->title) {
			return;
		}
	}

	free(view->window->title);
	free(view->window->formatted_title);
	if (title) {
		size_t len = parse_title_format(view, NULL);
		char *buffer = calloc(len + 1, sizeof(char));
		hayward_assert(buffer, "Unable to allocate title string");
		parse_title_format(view, buffer);

		view->window->title = strdup(title);
		view->window->formatted_title = buffer;
	} else {
		view->window->title = NULL;
		view->window->formatted_title = NULL;
	}

	// Update title after the global font height is updated
	window_update_title_textures(view->window);

	ipc_event_window(view->window, "title");

	if (view->foreign_toplevel && title) {
		wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title);
	}
}

bool view_is_visible(struct hayward_view *view) {
	if (view->window->node.destroying) {
		return false;
	}
	struct hayward_workspace *workspace = view->window->pending.workspace;
	if (!workspace) {
		return false;
	}

	if (!window_is_sticky(view->window) && workspace &&
			!workspace_is_visible(workspace)) {
		return false;
	}
	// Check view isn't in a stacked container on an inactive tab
	struct hayward_window *window = view->window;
	struct hayward_column *column = window->pending.parent;
	if (column != NULL) {
		enum hayward_column_layout parent_layout = column->pending.layout;
		if (parent_layout == L_STACKED && column->pending.active_child != window) {
			return false;
		}
	}

	// Check view isn't hidden by another fullscreen view
	struct hayward_window *fs = workspace->pending.fullscreen;
	if (fs && !window_is_fullscreen(view->window) &&
			!window_is_transient_for(view->window, fs)) {
		return false;
	}
	return true;
}

void view_set_urgent(struct hayward_view *view, bool enable) {
	if (view_is_urgent(view) == enable) {
		return;
	}
	if (enable) {
		struct hayward_seat *seat = input_manager_current_seat();
		if (seat_get_focused_container(seat) == view->window) {
			return;
		}
		clock_gettime(CLOCK_MONOTONIC, &view->urgent);
	} else {
		view->urgent = (struct timespec){ 0 };
		if (view->urgent_timer) {
			wl_event_source_remove(view->urgent_timer);
			view->urgent_timer = NULL;
		}
	}
	desktop_damage_window(view->window);

	ipc_event_window(view->window, "urgent");

	workspace_detect_urgent(view->window->pending.workspace);
}

bool view_is_urgent(struct hayward_view *view) {
	return view->urgent.tv_sec || view->urgent.tv_nsec;
}

void view_remove_saved_buffer(struct hayward_view *view) {
	hayward_assert(!wl_list_empty(&view->saved_buffers), "Expected a saved buffer");
	struct hayward_saved_buffer *saved_buf, *tmp;
	wl_list_for_each_safe(saved_buf, tmp, &view->saved_buffers, link) {
		wlr_buffer_unlock(&saved_buf->buffer->base);
		wl_list_remove(&saved_buf->link);
		free(saved_buf);
	}
}

static void view_save_buffer_iterator(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct hayward_view *view = data;

	if (surface && wlr_surface_has_buffer(surface)) {
		wlr_buffer_lock(&surface->buffer->base);
		struct hayward_saved_buffer *saved_buffer = calloc(1, sizeof(struct hayward_saved_buffer));
		saved_buffer->buffer = surface->buffer;
		saved_buffer->width = surface->current.width;
		saved_buffer->height = surface->current.height;
		saved_buffer->x = view->window->surface_x + sx;
		saved_buffer->y = view->window->surface_y + sy;
		saved_buffer->transform = surface->current.transform;
		wlr_surface_get_buffer_source_box(surface, &saved_buffer->source_box);
		wl_list_insert(view->saved_buffers.prev, &saved_buffer->link);
	}
}

void view_save_buffer(struct hayward_view *view) {
	hayward_assert(wl_list_empty(&view->saved_buffers), "Didn't expect saved buffer");
	view_for_each_surface(view, view_save_buffer_iterator, view);
}

bool view_is_transient_for(struct hayward_view *child,
		struct hayward_view *ancestor) {
	return child->impl->is_transient_for &&
		child->impl->is_transient_for(child, ancestor);
}
