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
#include "wmiiv/criteria.h"
#include "wmiiv/commands.h"
#include "wmiiv/desktop.h"
#include "wmiiv/desktop/transaction.h"
#include "wmiiv/desktop/idle_inhibit_v1.h"
#include "wmiiv/input/cursor.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/server.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/config.h"
#include "wmiiv/xdg_decoration.h"
#include "pango.h"
#include "stringop.h"

void view_init(struct wmiiv_view *view, enum wmiiv_view_type type,
		const struct wmiiv_view_impl *impl) {
	view->type = type;
	view->impl = impl;
	view->executed_criteria = create_list();
	wl_list_init(&view->saved_buffers);
	view->allow_request_urgent = true;
	view->shortcuts_inhibit = SHORTCUTS_INHIBIT_DEFAULT;
	wl_signal_init(&view->events.unmap);
}

void view_destroy(struct wmiiv_view *view) {
	if (!wmiiv_assert(view->surface == NULL, "Tried to free mapped view")) {
		return;
	}
	if (!wmiiv_assert(view->destroying,
				"Tried to free view which wasn't marked as destroying")) {
		return;
	}
	if (!wmiiv_assert(view->container == NULL,
				"Tried to free view which still has a container "
				"(might have a pending transaction?)")) {
		return;
	}
	wl_list_remove(&view->events.unmap.listener_list);
	if (!wl_list_empty(&view->saved_buffers)) {
		view_remove_saved_buffer(view);
	}
	list_free(view->executed_criteria);

	free(view->title_format);

	if (view->impl->destroy) {
		view->impl->destroy(view);
	} else {
		free(view);
	}
}

void view_begin_destroy(struct wmiiv_view *view) {
	if (!wmiiv_assert(view->surface == NULL, "Tried to destroy a mapped view")) {
		return;
	}
	view->destroying = true;

	if (!view->container) {
		view_destroy(view);
	}
}

const char *view_get_title(struct wmiiv_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_TITLE);
	}
	return NULL;
}

const char *view_get_app_id(struct wmiiv_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_APP_ID);
	}
	return NULL;
}

const char *view_get_class(struct wmiiv_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_CLASS);
	}
	return NULL;
}

const char *view_get_instance(struct wmiiv_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_INSTANCE);
	}
	return NULL;
}
#if HAVE_XWAYLAND
uint32_t view_get_x11_window_id(struct wmiiv_view *view) {
	if (view->impl->get_int_prop) {
		return view->impl->get_int_prop(view, VIEW_PROP_X11_WINDOW_ID);
	}
	return 0;
}

uint32_t view_get_x11_parent_id(struct wmiiv_view *view) {
	if (view->impl->get_int_prop) {
		return view->impl->get_int_prop(view, VIEW_PROP_X11_PARENT_ID);
	}
	return 0;
}
#endif
const char *view_get_window_role(struct wmiiv_view *view) {
	if (view->impl->get_string_prop) {
		return view->impl->get_string_prop(view, VIEW_PROP_WINDOW_ROLE);
	}
	return NULL;
}

uint32_t view_get_window_type(struct wmiiv_view *view) {
	if (view->impl->get_int_prop) {
		return view->impl->get_int_prop(view, VIEW_PROP_WINDOW_TYPE);
	}
	return 0;
}

const char *view_get_shell(struct wmiiv_view *view) {
	switch(view->type) {
	case WMIIV_VIEW_XDG_SHELL:
		return "xdg_shell";
#if HAVE_XWAYLAND
	case WMIIV_VIEW_XWAYLAND:
		return "xwayland";
#endif
	}
	return "unknown";
}

void view_get_constraints(struct wmiiv_view *view, double *min_width,
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

uint32_t view_configure(struct wmiiv_view *view, double lx, double ly, int width,
		int height) {
	if (view->impl->configure) {
		return view->impl->configure(view, lx, ly, width, height);
	}
	return 0;
}

bool view_inhibit_idle(struct wmiiv_view *view) {
	struct wmiiv_idle_inhibitor_v1 *user_inhibitor =
		wmiiv_idle_inhibit_v1_user_inhibitor_for_view(view);

	struct wmiiv_idle_inhibitor_v1 *application_inhibitor =
		wmiiv_idle_inhibit_v1_application_inhibitor_for_view(view);

	if (!user_inhibitor && !application_inhibitor) {
		return false;
	}

	if (!user_inhibitor) {
		return wmiiv_idle_inhibit_v1_is_active(application_inhibitor);
	}

	if (!application_inhibitor) {
		return wmiiv_idle_inhibit_v1_is_active(user_inhibitor);
	}

	return wmiiv_idle_inhibit_v1_is_active(user_inhibitor)
		|| wmiiv_idle_inhibit_v1_is_active(application_inhibitor);
}

bool view_ancestor_is_only_visible(struct wmiiv_view *view) {
	bool only_visible = true;
	struct wmiiv_container *container = view->container;
	while (container) {
		enum wmiiv_container_layout layout = container_parent_layout(container);
		if (layout != L_TABBED && layout != L_STACKED) {
			list_t *siblings = container_get_siblings(container);
			if (siblings && siblings->length > 1) {
				only_visible = false;
			}
		} else {
			only_visible = true;
		}
		container = container->pending.parent;
	}
	return only_visible;
}

static bool view_is_only_visible(struct wmiiv_view *view) {
	struct wmiiv_container *container = view->container;
	while (container) {
		enum wmiiv_container_layout layout = container_parent_layout(container);
		if (layout != L_TABBED && layout != L_STACKED) {
			list_t *siblings = container_get_siblings(container);
			if (siblings && siblings->length > 1) {
				return false;
			}
		}

		container = container->pending.parent;
	}

	return true;
}

static bool gaps_to_edge(struct wmiiv_view *view) {
	struct side_gaps gaps = view->container->pending.workspace->current_gaps;
	return gaps.top > 0 || gaps.right > 0 || gaps.bottom > 0 || gaps.left > 0;
}

void view_autoconfigure(struct wmiiv_view *view) {
	struct wmiiv_container *window = view->container;
	struct wmiiv_workspace *workspace = window->pending.workspace;

	struct wmiiv_output *output = workspace ? workspace->output : NULL;

	if (window->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		window->pending.content_x = output->lx;
		window->pending.content_y = output->ly;
		window->pending.content_width = output->width;
		window->pending.content_height = output->height;
		return;
	} else if (window->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		window->pending.content_x = root->x;
		window->pending.content_y = root->y;
		window->pending.content_width = root->width;
		window->pending.content_height = root->height;
		return;
	}

	window->pending.border_top = window->pending.border_bottom = true;
	window->pending.border_left = window->pending.border_right = true;
	double y_offset = 0;

	if (!window_is_floating(window) && workspace) {
		if (config->hide_edge_borders == E_BOTH
				|| config->hide_edge_borders == E_VERTICAL) {
			window->pending.border_left = window->pending.x != workspace->x;
			int right_x = window->pending.x + window->pending.width;
			window->pending.border_right = right_x != workspace->x + workspace->width;
		}

		if (config->hide_edge_borders == E_BOTH
				|| config->hide_edge_borders == E_HORIZONTAL) {
			window->pending.border_top = window->pending.y != workspace->y;
			int bottom_y = window->pending.y + window->pending.height;
			window->pending.border_bottom = bottom_y != workspace->y + workspace->height;
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
		// In a tabbed or stacked container, the container's y is the top of the
		// title area. We have to offset the surface y by the height of the title,
		// bar, and disable any top border because we'll always have the title bar.
		list_t *siblings = container_get_siblings(window);
		bool show_titlebar = (siblings && siblings->length > 1)
			|| !config->hide_lone_tab;
		if (show_titlebar) {
			enum wmiiv_container_layout layout = container_parent_layout(window);
			if (layout == L_TABBED) {
				y_offset = container_titlebar_height();
				window->pending.border_top = false;
			} else if (layout == L_STACKED) {
				y_offset = container_titlebar_height() * siblings->length;
				window->pending.border_top = false;
			}
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
		height = window->pending.height - y_offset;
		break;
	case B_PIXEL:
		x = window->pending.x + window->pending.border_thickness * window->pending.border_left;
		y = window->pending.y + window->pending.border_thickness * window->pending.border_top + y_offset;
		width = window->pending.width
			- window->pending.border_thickness * window->pending.border_left
			- window->pending.border_thickness * window->pending.border_right;
		height = window->pending.height - y_offset
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
			height = window->pending.height - y_offset
				- window->pending.border_thickness * window->pending.border_bottom;
		} else {
			y = window->pending.y + container_titlebar_height();
			height = window->pending.height - container_titlebar_height()
				- window->pending.border_thickness * window->pending.border_bottom;
		}
		break;
	}

	window->pending.content_x = x;
	window->pending.content_y = y;
	window->pending.content_width = width;
	window->pending.content_height = height;
}

void view_set_activated(struct wmiiv_view *view, bool activated) {
	if (view->impl->set_activated) {
		view->impl->set_activated(view, activated);
	}
	if (view->foreign_toplevel) {
		wlr_foreign_toplevel_handle_v1_set_activated(
				view->foreign_toplevel, activated);
	}
}

void view_request_activate(struct wmiiv_view *view) {
	struct wmiiv_workspace *workspace = view->container->pending.workspace;
	struct wmiiv_seat *seat = input_manager_current_seat();

	switch (config->focus_on_window_activation) {
	case FOWA_SMART:
		if (workspace_is_visible(workspace)) {
			seat_set_focus_window(seat, view->container);
		} else {
			view_set_urgent(view, true);
		}
		break;
	case FOWA_URGENT:
		view_set_urgent(view, true);
		break;
	case FOWA_FOCUS:
		seat_set_focus_window(seat, view->container);
		break;
	case FOWA_NONE:
		break;
	}
}

void view_set_csd_from_server(struct wmiiv_view *view, bool enabled) {
	wmiiv_log(WMIIV_DEBUG, "Telling view %p to set CSD to %i", view, enabled);
	if (view->xdg_decoration) {
		uint32_t mode = enabled ?
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE :
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
		wlr_xdg_toplevel_decoration_v1_set_mode(
				view->xdg_decoration->wlr_xdg_decoration, mode);
	}
	view->using_csd = enabled;
}

void view_update_csd_from_client(struct wmiiv_view *view, bool enabled) {
	wmiiv_log(WMIIV_DEBUG, "View %p updated CSD to %i", view, enabled);
	struct wmiiv_container *window = view->container;
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

void view_set_tiled(struct wmiiv_view *view, bool tiled) {
	if (view->impl->set_tiled) {
		view->impl->set_tiled(view, tiled);
	}
}

void view_close(struct wmiiv_view *view) {
	if (view->impl->close) {
		view->impl->close(view);
	}
}

void view_close_popups(struct wmiiv_view *view) {
	if (view->impl->close_popups) {
		view->impl->close_popups(view);
	}
}

void view_damage_from(struct wmiiv_view *view) {
	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		output_damage_from_view(output, view);
	}
}

void view_for_each_surface(struct wmiiv_view *view,
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

void view_for_each_popup_surface(struct wmiiv_view *view,
		wlr_surface_iterator_func_t iterator, void *user_data) {
	if (!view->surface) {
		return;
	}
	if (view->impl->for_each_popup_surface) {
		view->impl->for_each_popup_surface(view, iterator, user_data);
	}
}

static void view_subsurface_create(struct wmiiv_view *view,
	struct wlr_subsurface *subsurface);

static void view_init_subsurfaces(struct wmiiv_view *view,
	struct wlr_surface *surface);

static void view_child_init_subsurfaces(struct wmiiv_view_child *view_child,
	struct wlr_surface *surface);

static void view_handle_surface_new_subsurface(struct wl_listener *listener,
		void *data) {
	struct wmiiv_view *view =
		wl_container_of(listener, view, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;
	view_subsurface_create(view, subsurface);
}

static bool view_has_executed_criteria(struct wmiiv_view *view,
		struct criteria *criteria) {
	for (int i = 0; i < view->executed_criteria->length; ++i) {
		struct criteria *item = view->executed_criteria->items[i];
		if (item == criteria) {
			return true;
		}
	}
	return false;
}

void view_execute_criteria(struct wmiiv_view *view) {
	list_t *criterias = criteria_for_view(view, CT_COMMAND);
	for (int i = 0; i < criterias->length; i++) {
		struct criteria *criteria = criterias->items[i];
		wmiiv_log(WMIIV_DEBUG, "Checking criteria %s", criteria->raw);
		if (view_has_executed_criteria(view, criteria)) {
			wmiiv_log(WMIIV_DEBUG, "Criteria already executed");
			continue;
		}
		wmiiv_log(WMIIV_DEBUG, "for_window '%s' matches view %p, cmd: '%s'",
				criteria->raw, view, criteria->cmdlist);
		list_add(view->executed_criteria, criteria);
		list_t *res_list = execute_command(
				criteria->cmdlist, NULL, view->container);
		while (res_list->length) {
			struct cmd_results *res = res_list->items[0];
			free_cmd_results(res);
			list_del(res_list, 0);
		}
		list_free(res_list);
	}
	list_free(criterias);
}

static void view_populate_pid(struct wmiiv_view *view) {
	pid_t pid;
	switch (view->type) {
#if HAVE_XWAYLAND
	case WMIIV_VIEW_XWAYLAND:;
		struct wlr_xwayland_surface *surf =
			wlr_xwayland_surface_from_wlr_surface(view->surface);
		pid = surf->pid;
		break;
#endif
	case WMIIV_VIEW_XDG_SHELL:;
		struct wl_client *client =
			wl_resource_get_client(view->surface->resource);
		wl_client_get_credentials(client, &pid, NULL, NULL);
		break;
	}
	view->pid = pid;
}

static struct wmiiv_workspace *select_workspace(struct wmiiv_view *view) {
	struct wmiiv_seat *seat = input_manager_current_seat();

	// Check if there's any `assign` criteria for the view
	list_t *criterias = criteria_for_view(view,
			CT_ASSIGN_WORKSPACE | CT_ASSIGN_WORKSPACE_NUMBER | CT_ASSIGN_OUTPUT);
	struct wmiiv_workspace *workspace = NULL;
	for (int i = 0; i < criterias->length; ++i) {
		struct criteria *criteria = criterias->items[i];
		if (criteria->type == CT_ASSIGN_OUTPUT) {
			struct wmiiv_output *output = output_by_name_or_id(criteria->target);
			if (output) {
				workspace = output_get_active_workspace(output);
				break;
			}
		} else {
			// CT_ASSIGN_WORKSPACE(_NUMBER)
			workspace = criteria->type == CT_ASSIGN_WORKSPACE_NUMBER ?
				workspace_by_number(criteria->target) :
				workspace_by_name(criteria->target);

			if (!workspace) {
				if (strcasecmp(criteria->target, "back_and_forth") == 0) {
					if (seat->prev_workspace_name) {
						workspace = workspace_create(NULL, seat->prev_workspace_name);
					}
				} else {
					workspace = workspace_create(NULL, criteria->target);
				}
			}
			break;
		}
	}
	list_free(criterias);
	if (workspace) {
		root_remove_workspace_pid(view->pid);
		return workspace;
	}

	// Check if there's a PID mapping
	workspace = root_workspace_for_pid(view->pid);
	if (workspace) {
		return workspace;
	}

	// Use the focused workspace
	workspace = seat_get_focused_workspace(seat);
	if (workspace) {
		return workspace;
	}

	// When there's no outputs connected, the above should match a workspace on
	// the noop output.
	wmiiv_assert(false, "Expected to find a workspace");
	return NULL;
}

static bool should_focus(struct wmiiv_view *view) {
	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_container *prev_container = seat_get_focused_container(seat);
	struct wmiiv_workspace *prev_workspace = seat_get_focused_workspace(seat);
	struct wmiiv_workspace *map_workspace = view->container->pending.workspace;

	if (view->container->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		return true;
	}

	// View opened "under" fullscreen view should not be given focus.
	if (root->fullscreen_global || !map_workspace || map_workspace->fullscreen) {
		return false;
	}

	// Views can only take focus if they are mapped into the active workspace
	if (prev_workspace != map_workspace) {
		return false;
	}

	// If the view is the only one in the focused workspace, it'll get focus
	// regardless of any no_focus criteria.
	if (!view->container->pending.parent && !prev_container) {
		size_t num_children = view->container->pending.workspace->tiling->length +
			view->container->pending.workspace->floating->length;
		if (num_children == 1) {
			return true;
		}
	}

	// Check no_focus criteria
	list_t *criterias = criteria_for_view(view, CT_NO_FOCUS);
	size_t len = criterias->length;
	list_free(criterias);
	return len == 0;
}

static void handle_foreign_activate_request(
		struct wl_listener *listener, void *data) {
	struct wmiiv_view *view = wl_container_of(
			listener, view, foreign_activate_request);
	struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		if (seat->wlr_seat == event->seat) {
			seat_set_focus_window(seat, view->container);
			seat_consider_warp_to_focus(seat);
			container_raise_floating(view->container);
			break;
		}
	}
	transaction_commit_dirty();
}

static void handle_foreign_fullscreen_request(
		struct wl_listener *listener, void *data) {
	struct wmiiv_view *view = wl_container_of(
			listener, view, foreign_fullscreen_request);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

	struct wmiiv_container *container = view->container;

	if (event->fullscreen && event->output && event->output->data) {
		struct wmiiv_output *output = event->output->data;
		struct wmiiv_workspace *workspace = output_get_active_workspace(output);
		if (workspace) {
			if (window_is_floating(view->container)) {
				workspace_add_floating(workspace, view->container);
			} else {
				workspace_add_tiling(workspace, view->container);
			}
		}
	}

	container_set_fullscreen(container,
		event->fullscreen ? FULLSCREEN_WORKSPACE : FULLSCREEN_NONE);
	if (event->fullscreen) {
		arrange_root();
	} else {
		if (container->pending.parent) {
			arrange_column(container->pending.parent);
		} else if (container->pending.workspace) {
			arrange_workspace(container->pending.workspace);
		}
	}
	transaction_commit_dirty();
}

static void handle_foreign_close_request(
		struct wl_listener *listener, void *data) {
	struct wmiiv_view *view = wl_container_of(
			listener, view, foreign_close_request);
	view_close(view);
}

static void handle_foreign_destroy(
		struct wl_listener *listener, void *data) {
	struct wmiiv_view *view = wl_container_of(
			listener, view, foreign_destroy);

	wl_list_remove(&view->foreign_activate_request.link);
	wl_list_remove(&view->foreign_fullscreen_request.link);
	wl_list_remove(&view->foreign_close_request.link);
	wl_list_remove(&view->foreign_destroy.link);
}

void view_map(struct wmiiv_view *view, struct wlr_surface *wlr_surface,
			  bool fullscreen, struct wlr_output *fullscreen_output,
			  bool decoration) {
	if (!wmiiv_assert(view->surface == NULL, "cannot map mapped view")) {
		return;
	}
	view->surface = wlr_surface;
	view_populate_pid(view);
	view->container = window_create(view);

	// If there is a request to be opened fullscreen on a specific output, try
	// to honor that request. Otherwise, fallback to assigns, pid mappings,
	// focused workspace, etc
	struct wmiiv_workspace *workspace = NULL;
	if (fullscreen_output && fullscreen_output->data) {
		struct wmiiv_output *output = fullscreen_output->data;
		workspace = output_get_active_workspace(output);
	}
	if (!workspace) {
		workspace = select_workspace(view);
	}
	if (!wmiiv_assert(workspace, "Could not find workspace to map view to")) {
		return;
	}

	struct wmiiv_seat *seat = input_manager_current_seat();

	struct wmiiv_container *target_sibling = seat_get_focus_inactive_tiling(seat, workspace);
	if (target_sibling && container_is_column(target_sibling)) {
		// TODO (wmiiv) Shouldn't be possible once columns are no longer focusable.
		target_sibling = seat_get_focus_inactive_view(seat, &target_sibling->node);
	}

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

	if (target_sibling) {
		column_add_sibling(target_sibling, view->container, 1);
	} else if (workspace) {
		struct wmiiv_container *column = column_create();
		column_add_child(column, view->container);
		workspace_insert_tiling_direct(workspace, column, 0);
	}
	ipc_event_window(view->container, "new");

	view_init_subsurfaces(view, wlr_surface);
	wl_signal_add(&wlr_surface->events.new_subsurface,
			&view->surface_new_subsurface);
	view->surface_new_subsurface.notify = view_handle_surface_new_subsurface;

	if (decoration) {
		view_update_csd_from_client(view, decoration);
	}

	if (view->impl->wants_floating && view->impl->wants_floating(view)) {
		view->container->pending.border = config->floating_border;
		view->container->pending.border_thickness = config->floating_border_thickness;
		window_set_floating(view->container, true);
	} else {
		view->container->pending.border = config->border;
		view->container->pending.border_thickness = config->border_thickness;
		view_set_tiled(view, true);
	}

	if (config->popup_during_fullscreen == POPUP_LEAVE &&
			view->container->pending.workspace &&
			view->container->pending.workspace->fullscreen &&
			view->container->pending.workspace->fullscreen->view) {
		struct wmiiv_container *fs = view->container->pending.workspace->fullscreen;
		if (view_is_transient_for(view, fs->view)) {
			container_set_fullscreen(fs, false);
		}
	}

	view_update_title(view, false);
	if (view->container->pending.parent) {
		column_update_representation(view->container->pending.parent);
	} else {
		workspace_update_representation(view->container->pending.workspace);
	}

	if (fullscreen) {
		container_set_fullscreen(view->container, true);
		arrange_workspace(view->container->pending.workspace);
	} else {
		if (target_sibling) {
			arrange_column(view->container->pending.parent);
		} else if (view->container->pending.workspace) {
			arrange_workspace(view->container->pending.workspace);
		}
	}

	view_execute_criteria(view);

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
		input_manager_set_focus(&view->container->node);
	}

	const char *app_id;
	const char *class;
	if ((app_id = view_get_app_id(view)) != NULL) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, app_id);
	} else if ((class = view_get_class(view)) != NULL) {
		wlr_foreign_toplevel_handle_v1_set_app_id(view->foreign_toplevel, class);
	}
}

void view_unmap(struct wmiiv_view *view) {
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

	struct wmiiv_container *parent = view->container->pending.parent;
	struct wmiiv_workspace *workspace = view->container->pending.workspace;
	container_begin_destroy(view->container);
	if (parent) {
		column_consider_destroy(parent);
	} else if (workspace) {
		// TODO (wmiiv) shouldn't be possible.
		workspace_consider_destroy(workspace);
	}

	if (root->fullscreen_global) {
		// Container may have been a child of the root fullscreen container
		arrange_root();
	} else if (workspace && !workspace->node.destroying) {
		arrange_workspace(workspace);
		workspace_detect_urgent(workspace);
	}

	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat->cursor->image_surface = NULL;
		if (seat->cursor->active_constraint) {
			struct wlr_surface *constrain_surface =
				seat->cursor->active_constraint->surface;
			if (view_from_wlr_surface(constrain_surface) == view) {
				wmiiv_cursor_constrain(seat->cursor, NULL);
			}
		}
		seat_consider_warp_to_focus(seat);
	}

	transaction_commit_dirty();
	view->surface = NULL;
}

void view_update_size(struct wmiiv_view *view) {
	struct wmiiv_container *container = view->container;
	container->pending.content_width = view->geometry.width;
	container->pending.content_height = view->geometry.height;
	container_set_geometry_from_content(container);
}

void view_center_surface(struct wmiiv_view *view) {
	struct wmiiv_container *container = view->container;
	// We always center the current coordinates rather than the next, as the
	// geometry immediately affects the currently active rendering.
	container->surface_x = fmax(container->current.content_x, container->current.content_x +
			(container->current.content_width - view->geometry.width) / 2);
	container->surface_y = fmax(container->current.content_y, container->current.content_y +
			(container->current.content_height - view->geometry.height) / 2);
}

static const struct wmiiv_view_child_impl subsurface_impl;

static void subsurface_get_view_coords(struct wmiiv_view_child *child,
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

static void subsurface_destroy(struct wmiiv_view_child *child) {
	if (!wmiiv_assert(child->impl == &subsurface_impl,
			"Expected a subsurface")) {
		return;
	}
	struct wmiiv_subsurface *subsurface = (struct wmiiv_subsurface *)child;
	wl_list_remove(&subsurface->destroy.link);
	free(subsurface);
}

static const struct wmiiv_view_child_impl subsurface_impl = {
	.get_view_coords = subsurface_get_view_coords,
	.destroy = subsurface_destroy,
};

static void subsurface_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct wmiiv_subsurface *subsurface =
		wl_container_of(listener, subsurface, destroy);
	struct wmiiv_view_child *child = &subsurface->child;
	view_child_destroy(child);
}

static void view_child_damage(struct wmiiv_view_child *child, bool whole);

static void view_subsurface_create(struct wmiiv_view *view,
		struct wlr_subsurface *wlr_subsurface) {
	struct wmiiv_subsurface *subsurface =
		calloc(1, sizeof(struct wmiiv_subsurface));
	if (subsurface == NULL) {
		wmiiv_log(WMIIV_ERROR, "Allocation failed");
		return;
	}
	view_child_init(&subsurface->child, &subsurface_impl, view,
		wlr_subsurface->surface);

	wl_signal_add(&wlr_subsurface->events.destroy, &subsurface->destroy);
	subsurface->destroy.notify = subsurface_handle_destroy;

	subsurface->child.mapped = true;

	view_child_damage(&subsurface->child, true);
}

static void view_child_subsurface_create(struct wmiiv_view_child *child,
		struct wlr_subsurface *wlr_subsurface) {
	struct wmiiv_subsurface *subsurface =
		calloc(1, sizeof(struct wmiiv_subsurface));
	if (subsurface == NULL) {
		wmiiv_log(WMIIV_ERROR, "Allocation failed");
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

static bool view_child_is_mapped(struct wmiiv_view_child *child) {
	while (child) {
		if (!child->mapped) {
			return false;
		}
		child = child->parent;
	}
	return true;
}

static void view_child_damage(struct wmiiv_view_child *child, bool whole) {
	if (!child || !view_child_is_mapped(child) || !child->view || !child->view->container) {
		return;
	}
	int sx, sy;
	child->impl->get_view_coords(child, &sx, &sy);
	desktop_damage_surface(child->surface,
			child->view->container->pending.content_x -
				child->view->geometry.x + sx,
			child->view->container->pending.content_y -
				child->view->geometry.y + sy, whole);
}

static void view_child_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct wmiiv_view_child *child =
		wl_container_of(listener, child, surface_commit);
	view_child_damage(child, false);
}

static void view_child_handle_surface_new_subsurface(
		struct wl_listener *listener, void *data) {
	struct wmiiv_view_child *child =
		wl_container_of(listener, child, surface_new_subsurface);
	struct wlr_subsurface *subsurface = data;
	view_child_subsurface_create(child, subsurface);
}

static void view_child_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct wmiiv_view_child *child =
		wl_container_of(listener, child, surface_destroy);
	view_child_destroy(child);
}

static void view_init_subsurfaces(struct wmiiv_view *view,
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

static void view_child_init_subsurfaces(struct wmiiv_view_child *view_child,
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
	struct wmiiv_view_child *child =
		wl_container_of(listener, child, surface_map);
	child->mapped = true;
	view_child_damage(child, true);
}

static void view_child_handle_surface_unmap(struct wl_listener *listener,
		void *data) {
	struct wmiiv_view_child *child =
		wl_container_of(listener, child, surface_unmap);
	view_child_damage(child, true);
	child->mapped = false;
}

static void view_child_handle_view_unmap(struct wl_listener *listener,
		void *data) {
	struct wmiiv_view_child *child =
		wl_container_of(listener, child, view_unmap);
	view_child_damage(child, true);
	child->mapped = false;
}

void view_child_init(struct wmiiv_view_child *child,
		const struct wmiiv_view_child_impl *impl, struct wmiiv_view *view,
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

	struct wmiiv_container *container = child->view->container;
	if (container != NULL) {
		struct wmiiv_workspace *workspace = container->pending.workspace;
		if (workspace) {
			wlr_surface_send_enter(child->surface, workspace->output->wlr_output);
		}
	}

	view_child_init_subsurfaces(child, surface);
}

void view_child_destroy(struct wmiiv_view_child *child) {
	if (view_child_is_mapped(child) && child->view->container != NULL) {
		view_child_damage(child, true);
	}

	if (child->parent != NULL) {
		wl_list_remove(&child->link);
		child->parent = NULL;
	}

	struct wmiiv_view_child *subchild, *tmpchild;
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

struct wmiiv_view *view_from_wlr_surface(struct wlr_surface *wlr_surface) {
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
	wmiiv_log(WMIIV_DEBUG, "Surface of unknown type (role %s): %p",
		role, wlr_surface);
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
static size_t parse_title_format(struct wmiiv_view *view, char *buffer) {
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

void view_update_title(struct wmiiv_view *view, bool force) {
	const char *title = view_get_title(view);

	if (!force) {
		if (title && view->container->title &&
				strcmp(title, view->container->title) == 0) {
			return;
		}
		if (!title && !view->container->title) {
			return;
		}
	}

	free(view->container->title);
	free(view->container->formatted_title);
	if (title) {
		size_t len = parse_title_format(view, NULL);
		char *buffer = calloc(len + 1, sizeof(char));
		if (!wmiiv_assert(buffer, "Unable to allocate title string")) {
			return;
		}
		parse_title_format(view, buffer);

		view->container->title = strdup(title);
		view->container->formatted_title = buffer;
	} else {
		view->container->title = NULL;
		view->container->formatted_title = NULL;
	}

	// Update title after the global font height is updated
	window_update_title_textures(view->container);

	ipc_event_window(view->container, "title");

	if (view->foreign_toplevel && title) {
		wlr_foreign_toplevel_handle_v1_set_title(view->foreign_toplevel, title);
	}
}

bool view_is_visible(struct wmiiv_view *view) {
	if (view->container->node.destroying) {
		return false;
	}
	struct wmiiv_workspace *workspace = view->container->pending.workspace;
	if (!workspace && view->container->pending.fullscreen_mode != FULLSCREEN_GLOBAL) {
		bool fs_global_descendant = false;
		struct wmiiv_container *parent = view->container->pending.parent;
		while (parent) {
			if (parent->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
				fs_global_descendant = true;
			}
			parent = parent->pending.parent;
		}
		if (!fs_global_descendant) {
			return false;
		}
	}

	if (!container_is_sticky_or_child(view->container) && workspace &&
			!workspace_is_visible(workspace)) {
		return false;
	}
	// Check view isn't in a tabbed or stacked container on an inactive tab
	struct wmiiv_seat *seat = input_manager_current_seat();
	struct wmiiv_container *window = view->container;
	struct wmiiv_container *column = window->pending.parent;
	if (column != NULL) {
		enum wmiiv_container_layout parent_layout = column->pending.layout;
		if (parent_layout == L_TABBED || parent_layout == L_STACKED) {
			if (seat_get_active_tiling_child(seat, &column->node) != &window->node) {
				return false;
			}
		}
	}

	// Check view isn't hidden by another fullscreen view
	struct wmiiv_container *fs = root->fullscreen_global ?
		root->fullscreen_global : workspace->fullscreen;
	if (fs && !window_is_fullscreen(view->container) &&
			!container_is_transient_for(view->container, fs)) {
		return false;
	}
	return true;
}

void view_set_urgent(struct wmiiv_view *view, bool enable) {
	if (view_is_urgent(view) == enable) {
		return;
	}
	if (enable) {
		struct wmiiv_seat *seat = input_manager_current_seat();
		if (seat_get_focused_container(seat) == view->container) {
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
	window_damage_whole(view->container);

	ipc_event_window(view->container, "urgent");

	workspace_detect_urgent(view->container->pending.workspace);
}

bool view_is_urgent(struct wmiiv_view *view) {
	return view->urgent.tv_sec || view->urgent.tv_nsec;
}

void view_remove_saved_buffer(struct wmiiv_view *view) {
	if (!wmiiv_assert(!wl_list_empty(&view->saved_buffers), "Expected a saved buffer")) {
		return;
	}
	struct wmiiv_saved_buffer *saved_buf, *tmp;
	wl_list_for_each_safe(saved_buf, tmp, &view->saved_buffers, link) {
		wlr_buffer_unlock(&saved_buf->buffer->base);
		wl_list_remove(&saved_buf->link);
		free(saved_buf);
	}
}

static void view_save_buffer_iterator(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct wmiiv_view *view = data;

	if (surface && wlr_surface_has_buffer(surface)) {
		wlr_buffer_lock(&surface->buffer->base);
		struct wmiiv_saved_buffer *saved_buffer = calloc(1, sizeof(struct wmiiv_saved_buffer));
		saved_buffer->buffer = surface->buffer;
		saved_buffer->width = surface->current.width;
		saved_buffer->height = surface->current.height;
		saved_buffer->x = view->container->surface_x + sx;
		saved_buffer->y = view->container->surface_y + sy;
		saved_buffer->transform = surface->current.transform;
		wlr_surface_get_buffer_source_box(surface, &saved_buffer->source_box);
		wl_list_insert(view->saved_buffers.prev, &saved_buffer->link);
	}
}

void view_save_buffer(struct wmiiv_view *view) {
	if (!wmiiv_assert(wl_list_empty(&view->saved_buffers), "Didn't expect saved buffer")) {
		view_remove_saved_buffer(view);
	}
	view_for_each_surface(view, view_save_buffer_iterator, view);
}

bool view_is_transient_for(struct wmiiv_view *child,
		struct wmiiv_view *ancestor) {
	return child->impl->is_transient_for &&
		child->impl->is_transient_for(child, ancestor);
}
