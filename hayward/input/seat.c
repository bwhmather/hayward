#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <linux/input-event-codes.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include "config.h"
#include "list.h"
#include "log.h"
#include "hayward/config.h"
#include "hayward/desktop.h"
#include "hayward/input/cursor.h"
#include "hayward/input/input-manager.h"
#include "hayward/input/keyboard.h"
#include "hayward/input/libinput.h"
#include "hayward/input/seat.h"
#include "hayward/input/switch.h"
#include "hayward/input/tablet.h"
#include "hayward/ipc-server.h"
#include "hayward/layers.h"
#include "hayward/output.h"
#include "hayward/server.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/column.h"
#include "hayward/tree/window.h"
#include "hayward/tree/root.h"
#include "hayward/tree/view.h"
#include "hayward/tree/workspace.h"

static void seat_device_destroy(struct hayward_seat_device *seat_device) {
	if (!seat_device) {
		return;
	}

	hayward_keyboard_destroy(seat_device->keyboard);
	hayward_tablet_destroy(seat_device->tablet);
	hayward_tablet_pad_destroy(seat_device->tablet_pad);
	hayward_switch_destroy(seat_device->switch_device);
	wlr_cursor_detach_input_device(seat_device->hayward_seat->cursor->cursor,
		seat_device->input_device->wlr_device);
	wl_list_remove(&seat_device->link);
	free(seat_device);
}

static void seat_window_destroy(struct hayward_seat_window *seat_window) {
	wl_list_remove(&seat_window->destroy.link);
	wl_list_remove(&seat_window->link);

	/*
	 * This is the only time we remove items from the focus stack without
	 * immediately re-adding them. If we just removed the last thing,
	 * mark that nothing has focus anymore.
	 */
	if (wl_list_empty(&seat_window->seat->active_window_stack)) {
		seat_window->seat->has_focus = false;
	}

	free(seat_window);
}

static void seat_workspace_destroy(struct hayward_seat_workspace *seat_workspace) {
	wl_list_remove(&seat_workspace->destroy.link);
	wl_list_remove(&seat_workspace->link);

	free(seat_workspace);
}

void seat_destroy(struct hayward_seat *seat) {
	if (seat == config->handler_context.seat) {
		config->handler_context.seat = input_manager_get_default_seat();
	}
	struct hayward_seat_device *seat_device, *next;
	wl_list_for_each_safe(seat_device, next, &seat->devices, link) {
		seat_device_destroy(seat_device);
	}
	struct hayward_seat_window *seat_window, *next_seat_window;
	wl_list_for_each_safe(seat_window, next_seat_window, &seat->active_window_stack,
			link) {
		seat_window_destroy(seat_window);
	}
	struct hayward_seat_workspace *seat_workspace, *next_seat_workspace;
	wl_list_for_each_safe(seat_workspace, next_seat_workspace, &seat->active_workspace_stack, link) {
		seat_workspace_destroy(seat_workspace);
	}

	hayward_input_method_relay_finish(&seat->im_relay);
	hayward_cursor_destroy(seat->cursor);
	wl_list_remove(&seat->new_node.link);
	wl_list_remove(&seat->request_start_drag.link);
	wl_list_remove(&seat->start_drag.link);
	wl_list_remove(&seat->request_set_selection.link);
	wl_list_remove(&seat->request_set_primary_selection.link);
	wl_list_remove(&seat->link);
	wlr_seat_destroy(seat->wlr_seat);
	for (int i = 0; i < seat->deferred_bindings->length; i++) {
		free_hayward_binding(seat->deferred_bindings->items[i]);
	}
	list_free(seat->deferred_bindings);
	free(seat->prev_workspace_name);
	free(seat);
}

void seat_idle_notify_activity(struct hayward_seat *seat,
		enum hayward_input_idle_source source) {
	uint32_t mask = seat->idle_inhibit_sources;
	struct wlr_idle_timeout *timeout;
	int ntimers = 0, nidle = 0;
	wl_list_for_each(timeout, &server.idle->idle_timers, link) {
		++ntimers;
		if (timeout->idle_state) {
			++nidle;
		}
	}
	if (nidle == ntimers) {
		mask = seat->idle_wake_sources;
	}
	if ((source & mask) > 0) {
		wlr_idle_notify_activity(server.idle, seat->wlr_seat);
	}
}

static struct hayward_keyboard *hayward_keyboard_for_wlr_keyboard(
		struct hayward_seat *seat, struct wlr_keyboard *wlr_keyboard) {
	struct hayward_seat_device *seat_device;
	wl_list_for_each(seat_device, &seat->devices, link) {
		struct hayward_input_device *input_device = seat_device->input_device;
		if (input_device->wlr_device->type != WLR_INPUT_DEVICE_KEYBOARD) {
			continue;
		}
		if (input_device->wlr_device->keyboard == wlr_keyboard) {
			return seat_device->keyboard;
		}
	}
	struct hayward_keyboard_group *group;
	wl_list_for_each(group, &seat->keyboard_groups, link) {
		struct hayward_input_device *input_device =
			group->seat_device->input_device;
		if (input_device->wlr_device->keyboard == wlr_keyboard) {
			return group->seat_device->keyboard;
		}
	}
	return NULL;
}

static void seat_keyboard_notify_enter(struct hayward_seat *seat,
		struct wlr_surface *surface) {
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
	if (!keyboard) {
		wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface, NULL, 0, NULL);
		return;
	}

	struct hayward_keyboard *hayward_keyboard =
		hayward_keyboard_for_wlr_keyboard(seat, keyboard);
	assert(hayward_keyboard && "Cannot find hayward_keyboard for seat keyboard");

	struct hayward_shortcut_state *state = &hayward_keyboard->state_pressed_sent;
	wlr_seat_keyboard_notify_enter(seat->wlr_seat, surface,
			state->pressed_keycodes, state->npressed, &keyboard->modifiers);
}

static void seat_tablet_pads_notify_enter(struct hayward_seat *seat,
		struct wlr_surface *surface) {
	struct hayward_seat_device *seat_device;
	wl_list_for_each(seat_device, &seat->devices, link) {
		hayward_tablet_pad_notify_enter(seat_device->tablet_pad, surface);
	}
}

/**
 * If container is a view, set it as active and enable keyboard input.
 * If container is a container, set all child views as active and don't enable
 * keyboard input on any.
 */
static void seat_send_focus(struct hayward_node *node, struct hayward_seat *seat) {
	hayward_assert(node_is_view(node), "Can only focus windows");

	if (!seat_is_input_allowed(seat, node->hayward_window->view->surface)) {
		hayward_log(HAYWARD_DEBUG, "Refusing to set focus, input is inhibited");
		return;
	}

	view_set_activated(node->hayward_window->view, true);
	struct hayward_view *view = node->hayward_window->view;
#if HAVE_XWAYLAND
	if (view->type == HAYWARD_VIEW_XWAYLAND) {
		struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
		wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
	}
#endif

	seat_keyboard_notify_enter(seat, view->surface);
	seat_tablet_pads_notify_enter(seat, view->surface);
	hayward_input_method_relay_set_focus(&seat->im_relay, view->surface);

	struct wlr_pointer_constraint_v1 *constraint =
		wlr_pointer_constraints_v1_constraint_for_surface(
			server.pointer_constraints, view->surface, seat->wlr_seat);
	hayward_cursor_constrain(seat->cursor, constraint);
}

void hayward_force_focus(struct wlr_surface *surface) {
	struct hayward_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seat_keyboard_notify_enter(seat, surface);
		seat_tablet_pads_notify_enter(seat, surface);
		hayward_input_method_relay_set_focus(&seat->im_relay, surface);
	}
}

void seat_for_each_node(struct hayward_seat *seat,
		void (*f)(struct hayward_node *node, void *data), void *data) {
	struct hayward_seat_window *current = NULL;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		f(&current->window->node, data);
	}
}

void seat_for_each_window(struct hayward_seat *seat,
		void (*f)(struct hayward_window *window, void *data), void *data) {
	struct hayward_seat_window *current = NULL;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		f(current->window, data);
	}
}

static void handle_workspace_destroy(struct wl_listener *listener, void *data) {
	struct hayward_seat_workspace *seat_workspace = wl_container_of(listener, seat_workspace, destroy);
	struct hayward_seat *seat = seat_workspace->seat;
	struct hayward_node *node = data;

	hayward_assert(node->type == N_WORKSPACE, "Expected workspace");
	struct hayward_workspace *workspace = node->hayward_workspace;

	hayward_assert(workspace == seat_workspace->workspace, "Destroy handler registered for different workspace");

	seat_workspace_destroy(seat_workspace);

	// If an unmanaged or layer surface is focused when an output gets
	// disabled and an empty workspace on the output was focused by the
	// seat, the seat needs to refocus its focus inactive to update the
	// value of seat->workspace.
	struct hayward_node *window_node = seat_get_focus_inactive(seat, &root->node);
	seat_set_focus(seat, window_node);
}

static void handle_window_destroy(struct wl_listener *listener, void *data) {
	struct hayward_seat_window *seat_window =
		wl_container_of(listener, seat_window, destroy);
	struct hayward_seat *seat = seat_window->seat;
	struct hayward_window *window = seat_window->window;
	struct hayward_node *focus = seat_get_focus(seat);

	if (&window->node == focus) {
		seat_set_focus_window(seat, NULL);
	}

	seat_window_destroy(seat_window);

	if (!window->pending.workspace) {
		// Window has already been removed from the tree.  Nothing to do.
		return;
	}

	if (&window->node != focus) {
		// Window wasn't focused, so removing it doesn't affect the focus
		// state.  Nothing to do.
		return;
	}

	// Find the next window to focus.
	// We would prefer to keep focus as close as possible to the closed window
	// so, instead of just picking the next window in the stack, we search back
	// for windows in the same column, the same workspace, and then in other
	// visible workspaces.  We do not switch between floating and tiling, and we
	// do not change the visible workspace for an output.
	struct hayward_window *new_focus = NULL;

	if (window_is_fullscreen(window)) {
		struct hayward_seat_window *candidate_seat_window;

		// Search for first floating, tiling or fullscreen container on a visible
		// workspace.
		wl_list_for_each(candidate_seat_window, &seat->active_window_stack, link) {
			struct hayward_window *candidate = candidate_seat_window->window;

			if (!workspace_is_visible(candidate->pending.workspace)) {
				continue;
			}

			new_focus = candidate;
			break;
		}

	} else if (window_is_floating(window)) {
		struct hayward_seat_window *candidate_seat_window;

		// Search other floating containers in all visible workspaces.
		wl_list_for_each(candidate_seat_window, &seat->active_window_stack, link) {
			struct hayward_window *candidate = candidate_seat_window->window;

			if (!window_is_floating(candidate)) {
				continue;
			}

			if (!workspace_is_visible(candidate->pending.workspace)) {
				continue;
			}

			new_focus = candidate;
			break;
		}

	} else {
		struct hayward_seat_window *candidate_seat_window;

		// Search for next tiling container in same column.
		wl_list_for_each(candidate_seat_window, &seat->active_window_stack, link) {
			struct hayward_window *candidate = candidate_seat_window->window;

			if (candidate->pending.parent != window->pending.parent) {
				continue;
			}

			new_focus = candidate;
			break;
		}

		// Search for next tiling container in same workspace.
		wl_list_for_each(candidate_seat_window, &seat->active_window_stack, link) {
			struct hayward_window *candidate = candidate_seat_window->window;

			if (candidate->pending.workspace != window->pending.workspace) {
				continue;
			}

			if (window_is_floating(candidate)) {
				continue;
			}

			if (window_is_fullscreen(candidate)) {
				continue;
			}

			new_focus = candidate;
			break;
		}

		// Search for next tiling container in all visible workspaces.
		wl_list_for_each(candidate_seat_window, &seat->active_window_stack, link) {
			struct hayward_window *candidate = candidate_seat_window->window;

			if (window_is_floating(candidate)) {
				continue;
			}

			if (window_is_fullscreen(candidate)) {
				continue;
			}

			if (!workspace_is_visible(candidate->pending.workspace)) {
				continue;
			}

			new_focus = candidate;
			break;
		}
	}

	if (new_focus) {
		seat_set_focus_window(seat, new_focus);
	}
}

static struct hayward_seat_workspace *seat_workspace_from_workspace(
	struct hayward_seat *seat, struct hayward_workspace *workspace) {
	struct hayward_seat_workspace *seat_workspace= NULL;
	wl_list_for_each(seat_workspace, &seat->active_workspace_stack, link) {
		if (seat_workspace->workspace == workspace) {
			return seat_workspace;
		}
	}

	seat_workspace = calloc(1, sizeof(struct hayward_seat_workspace));
	if (seat_workspace == NULL) {
		hayward_log(HAYWARD_ERROR, "could not allocate seat workspace");
		return NULL;
	}

	seat_workspace->workspace = workspace;
	seat_workspace->seat = seat;
	wl_list_insert(seat->active_workspace_stack.prev, &seat_workspace->link);
	wl_signal_add(&workspace->node.events.destroy, &seat_workspace->destroy);
	seat_workspace->destroy.notify = handle_workspace_destroy;

	return seat_workspace;
}

static struct hayward_seat_window *seat_window_from_window(
	struct hayward_seat *seat, struct hayward_window *window) {
	struct hayward_seat_window *seat_window = NULL;
	wl_list_for_each(seat_window, &seat->active_window_stack, link) {
		if (seat_window->window == window) {
			return seat_window;
		}
	}

	seat_window = calloc(1, sizeof(struct hayward_seat_window));
	if (seat_window == NULL) {
		hayward_log(HAYWARD_ERROR, "could not allocate seat node");
		return NULL;
	}

	seat_window->window = window;
	seat_window->seat = seat;
	wl_list_insert(seat->active_window_stack.prev, &seat_window->link);
	wl_signal_add(&window->node.events.destroy, &seat_window->destroy);
	seat_window->destroy.notify = handle_window_destroy;

	return seat_window;
}

static struct hayward_seat_window *seat_window_from_node(
		struct hayward_seat *seat, struct hayward_node *node) {
	if (node->type != N_WINDOW) {
		return NULL;
	}
	return seat_window_from_window(seat, node->hayward_window);
}

static void handle_new_node(struct wl_listener *listener, void *data) {
	struct hayward_seat *seat = wl_container_of(listener, seat, new_node);
	struct hayward_node *node = data;
	switch (node->type) {
	case N_ROOT:
		break;
	case N_OUTPUT:
		break;
	case N_WORKSPACE:
		seat_workspace_from_workspace(seat, node->hayward_workspace);
		break;
	case N_COLUMN:
		break;
	case N_WINDOW:
		seat_window_from_window(seat, node->hayward_window);
		break;
	}
}

static void drag_icon_damage_whole(struct hayward_drag_icon *icon) {
	if (!icon->wlr_drag_icon->mapped) {
		return;
	}
	desktop_damage_surface(icon->wlr_drag_icon->surface, icon->x, icon->y, true);
}

void drag_icon_update_position(struct hayward_drag_icon *icon) {
	drag_icon_damage_whole(icon);

	struct wlr_drag_icon *wlr_icon = icon->wlr_drag_icon;
	struct hayward_seat *seat = icon->seat;
	struct wlr_cursor *cursor = seat->cursor->cursor;
	switch (wlr_icon->drag->grab_type) {
	case WLR_DRAG_GRAB_KEYBOARD:
		return;
	case WLR_DRAG_GRAB_KEYBOARD_POINTER:
		icon->x = cursor->x + wlr_icon->surface->sx;
		icon->y = cursor->y + wlr_icon->surface->sy;
		break;
	case WLR_DRAG_GRAB_KEYBOARD_TOUCH:;
		struct wlr_touch_point *point =
			wlr_seat_touch_get_point(seat->wlr_seat, wlr_icon->drag->touch_id);
		if (point == NULL) {
			return;
		}
		icon->x = seat->touch_x + wlr_icon->surface->sx;
		icon->y = seat->touch_y + wlr_icon->surface->sy;
	}

	drag_icon_damage_whole(icon);
}

static void drag_icon_handle_surface_commit(struct wl_listener *listener,
		void *data) {
	struct hayward_drag_icon *icon =
		wl_container_of(listener, icon, surface_commit);
	drag_icon_update_position(icon);
}

static void drag_icon_handle_map(struct wl_listener *listener, void *data) {
	struct hayward_drag_icon *icon = wl_container_of(listener, icon, map);
	drag_icon_damage_whole(icon);
}

static void drag_icon_handle_unmap(struct wl_listener *listener, void *data) {
	struct hayward_drag_icon *icon = wl_container_of(listener, icon, unmap);
	drag_icon_damage_whole(icon);
}

static void drag_icon_handle_destroy(struct wl_listener *listener, void *data) {
	struct hayward_drag_icon *icon = wl_container_of(listener, icon, destroy);
	icon->wlr_drag_icon->data = NULL;
	wl_list_remove(&icon->link);
	wl_list_remove(&icon->surface_commit.link);
	wl_list_remove(&icon->unmap.link);
	wl_list_remove(&icon->map.link);
	wl_list_remove(&icon->destroy.link);
	free(icon);
}

static void drag_handle_destroy(struct wl_listener *listener, void *data) {
	struct hayward_drag *drag = wl_container_of(listener, drag, destroy);

	// Focus enter isn't sent during drag, so refocus the focused node, layer
	// surface or unmanaged surface.
	struct hayward_seat *seat = drag->seat;
	struct hayward_node *focus = seat_get_focus(seat);
	if (focus) {
		seat_set_focus(seat, NULL);
		seat_set_focus(seat, focus);
	} else if (seat->focused_layer) {
		struct wlr_layer_surface_v1 *layer = seat->focused_layer;
		seat_set_focus_layer(seat, NULL);
		seat_set_focus_layer(seat, layer);
	} else {
		struct wlr_surface *unmanaged = seat->wlr_seat->keyboard_state.focused_surface;
		seat_set_focus_surface(seat, NULL, false);
		seat_set_focus_surface(seat, unmanaged, false);
	}

	drag->wlr_drag->data = NULL;
	wl_list_remove(&drag->destroy.link);
	free(drag);
}

static void handle_request_start_drag(struct wl_listener *listener,
		void *data) {
	struct hayward_seat *seat = wl_container_of(listener, seat, request_start_drag);
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat->wlr_seat,
			event->origin, event->serial)) {
		wlr_seat_start_pointer_drag(seat->wlr_seat, event->drag, event->serial);
		return;
	}

	struct wlr_touch_point *point;
	if (wlr_seat_validate_touch_grab_serial(seat->wlr_seat,
			event->origin, event->serial, &point)) {
		wlr_seat_start_touch_drag(seat->wlr_seat,
			event->drag, event->serial, point);
		return;
	}

	// TODO: tablet grabs

	hayward_log(HAYWARD_DEBUG, "Ignoring start_drag request: "
		"could not validate pointer or touch serial %" PRIu32, event->serial);
	wlr_data_source_destroy(event->drag->source);
}

static void handle_start_drag(struct wl_listener *listener, void *data) {
	struct hayward_seat *seat = wl_container_of(listener, seat, start_drag);
	struct wlr_drag *wlr_drag = data;

	struct hayward_drag *drag = calloc(1, sizeof(struct hayward_drag));
	if (drag == NULL) {
		hayward_log(HAYWARD_ERROR, "Allocation failed");
		return;
	}
	drag->seat = seat;
	drag->wlr_drag = wlr_drag;
	wlr_drag->data = drag;

	drag->destroy.notify = drag_handle_destroy;
	wl_signal_add(&wlr_drag->events.destroy, &drag->destroy);

	struct wlr_drag_icon *wlr_drag_icon = wlr_drag->icon;
	if (wlr_drag_icon != NULL) {
		struct hayward_drag_icon *icon = calloc(1, sizeof(struct hayward_drag_icon));
		if (icon == NULL) {
			hayward_log(HAYWARD_ERROR, "Allocation failed");
			return;
		}
		icon->seat = seat;
		icon->wlr_drag_icon = wlr_drag_icon;
		wlr_drag_icon->data = icon;

		icon->surface_commit.notify = drag_icon_handle_surface_commit;
		wl_signal_add(&wlr_drag_icon->surface->events.commit, &icon->surface_commit);
		icon->unmap.notify = drag_icon_handle_unmap;
		wl_signal_add(&wlr_drag_icon->events.unmap, &icon->unmap);
		icon->map.notify = drag_icon_handle_map;
		wl_signal_add(&wlr_drag_icon->events.map, &icon->map);
		icon->destroy.notify = drag_icon_handle_destroy;
		wl_signal_add(&wlr_drag_icon->events.destroy, &icon->destroy);

		wl_list_insert(&root->drag_icons, &icon->link);

		drag_icon_update_position(icon);
	}
	seatop_begin_default(seat);
}

static void handle_request_set_selection(struct wl_listener *listener,
		void *data) {
	struct hayward_seat *seat =
		wl_container_of(listener, seat, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat->wlr_seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(struct wl_listener *listener,
		void *data) {
	struct hayward_seat *seat =
		wl_container_of(listener, seat, request_set_primary_selection);
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat->wlr_seat, event->source, event->serial);
}

static void collect_focus_iter(struct hayward_node *node, void *data) {
	struct hayward_seat *seat = data;
	struct hayward_seat_window *seat_window = seat_window_from_node(seat, node);
	if (!seat_window) {
		return;
	}
	wl_list_remove(&seat_window->link);
	wl_list_insert(&seat->active_window_stack, &seat_window->link);
}

static void collect_focus_workspace_iter(struct hayward_workspace *workspace,
		void *data) {
	collect_focus_iter(&workspace->node, data);
}

static void collect_focus_container_iter(struct hayward_window *container,
		void *data) {
	collect_focus_iter(&container->node, data);
}

struct hayward_seat *seat_create(const char *seat_name) {
	struct hayward_seat *seat = calloc(1, sizeof(struct hayward_seat));
	if (!seat) {
		return NULL;
	}

	seat->wlr_seat = wlr_seat_create(server.wl_display, seat_name);
	hayward_assert(seat->wlr_seat, "could not allocate seat");
	seat->wlr_seat->data = seat;

	seat->cursor = hayward_cursor_create(seat);
	if (!seat->cursor) {
		wlr_seat_destroy(seat->wlr_seat);
		free(seat);
		return NULL;
	}

	seat->idle_inhibit_sources = seat->idle_wake_sources =
		IDLE_SOURCE_KEYBOARD |
		IDLE_SOURCE_POINTER |
		IDLE_SOURCE_TOUCH |
		IDLE_SOURCE_TABLET_PAD |
		IDLE_SOURCE_TABLET_TOOL |
		IDLE_SOURCE_SWITCH;

	wl_list_init(&seat->active_window_stack);
	wl_list_init(&seat->active_workspace_stack);

	wl_list_init(&seat->devices);

	root_for_each_workspace(collect_focus_workspace_iter, seat);
	root_for_each_window(collect_focus_container_iter, seat);

	seat->deferred_bindings = create_list();

	wl_signal_add(&root->events.new_node, &seat->new_node);
	seat->new_node.notify = handle_new_node;

	wl_signal_add(&seat->wlr_seat->events.request_start_drag,
		&seat->request_start_drag);
	seat->request_start_drag.notify = handle_request_start_drag;

	wl_signal_add(&seat->wlr_seat->events.start_drag, &seat->start_drag);
	seat->start_drag.notify = handle_start_drag;

	wl_signal_add(&seat->wlr_seat->events.request_set_selection,
		&seat->request_set_selection);
	seat->request_set_selection.notify = handle_request_set_selection;

	wl_signal_add(&seat->wlr_seat->events.request_set_primary_selection,
		&seat->request_set_primary_selection);
	seat->request_set_primary_selection.notify =
		handle_request_set_primary_selection;

	wl_list_init(&seat->keyboard_groups);
	wl_list_init(&seat->keyboard_shortcuts_inhibitors);

	hayward_input_method_relay_init(seat, &seat->im_relay);

	bool first = wl_list_empty(&server.input->seats);
	wl_list_insert(&server.input->seats, &seat->link);

	if (!first) {
		// Since this is not the first seat, attempt to set initial focus
		struct hayward_seat *current_seat = input_manager_current_seat();
		struct hayward_node *current_focus =
			seat_get_focus_inactive(current_seat, &root->node);
		seat_set_focus(seat, current_focus);
	}

	seatop_begin_default(seat);

	return seat;
}

static void seat_update_capabilities(struct hayward_seat *seat) {
	uint32_t caps = 0;
	uint32_t previous_caps = seat->wlr_seat->capabilities;
	struct hayward_seat_device *seat_device;
	wl_list_for_each(seat_device, &seat->devices, link) {
		switch (seat_device->input_device->wlr_device->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			caps |= WL_SEAT_CAPABILITY_KEYBOARD;
			break;
		case WLR_INPUT_DEVICE_POINTER:
			caps |= WL_SEAT_CAPABILITY_POINTER;
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			caps |= WL_SEAT_CAPABILITY_TOUCH;
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			caps |= WL_SEAT_CAPABILITY_POINTER;
			break;
		case WLR_INPUT_DEVICE_SWITCH:
		case WLR_INPUT_DEVICE_TABLET_PAD:
			break;
		}
	}

	// Hide cursor if seat doesn't have pointer capability.
	// We must call cursor_set_image while the wlr_seat has the capabilities
	// otherwise it's a no op.
	if ((caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
		cursor_set_image(seat->cursor, NULL, NULL);
		wlr_seat_set_capabilities(seat->wlr_seat, caps);
	} else {
		wlr_seat_set_capabilities(seat->wlr_seat, caps);
		if ((previous_caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
			cursor_set_image(seat->cursor, "left_ptr", NULL);
		}
	}
}

static void seat_reset_input_config(struct hayward_seat *seat,
		struct hayward_seat_device *hayward_device) {
	hayward_log(HAYWARD_DEBUG, "Resetting output mapping for input device %s",
		hayward_device->input_device->identifier);
	wlr_cursor_map_input_to_output(seat->cursor->cursor,
		hayward_device->input_device->wlr_device, NULL);
}

static bool has_prefix(const char *str, const char *prefix) {
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

/**
 * Get the name of the built-in output, if any. Returns NULL if there isn't
 * exactly one built-in output.
 */
static const char *get_builtin_output_name(void) {
	const char *match = NULL;
	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *output = root->outputs->items[i];
		const char *name = output->wlr_output->name;
		if (has_prefix(name, "eDP-") || has_prefix(name, "LVDS-") ||
				has_prefix(name, "DSI-")) {
			if (match != NULL) {
				return NULL;
			}
			match = name;
		}
	}
	return match;
}

static bool is_touch_or_tablet_tool(struct hayward_seat_device *seat_device) {
	switch (seat_device->input_device->wlr_device->type) {
	case WLR_INPUT_DEVICE_TOUCH:
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return true;
	default:
		return false;
	}
}

static void seat_apply_input_config(struct hayward_seat *seat,
		struct hayward_seat_device *hayward_device) {
	struct input_config *ic =
		input_device_get_config(hayward_device->input_device);

	hayward_log(HAYWARD_DEBUG, "Applying input config to %s",
		hayward_device->input_device->identifier);

	const char *mapped_to_output = ic == NULL ? NULL : ic->mapped_to_output;
	struct wlr_box *mapped_to_region = ic == NULL ? NULL : ic->mapped_to_region;
	enum input_config_mapped_to mapped_to =
		ic == NULL ? MAPPED_TO_DEFAULT : ic->mapped_to;

	switch (mapped_to) {
	case MAPPED_TO_DEFAULT:;
		/*
		 * If the wlroots backend provides an output name, use that.
		 *
		 * Otherwise, try to map built-in touch and pointer devices to the
		 * built-in output.
		 */
		struct wlr_input_device *dev = hayward_device->input_device->wlr_device;
		switch (dev->type) {
		case WLR_INPUT_DEVICE_POINTER:
			mapped_to_output = dev->pointer->output_name;
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			mapped_to_output = dev->touch->output_name;
			break;
		default:
			mapped_to_output = NULL;
			break;
		}
		if (mapped_to_output == NULL && is_touch_or_tablet_tool(hayward_device) &&
				hayward_libinput_device_is_builtin(hayward_device->input_device)) {
			mapped_to_output = get_builtin_output_name();
			if (mapped_to_output) {
				hayward_log(HAYWARD_DEBUG, "Auto-detected output '%s' for device '%s'",
					mapped_to_output, hayward_device->input_device->identifier);
			}
		}
		if (mapped_to_output == NULL) {
			return;
		}
		/* fallthrough */
	case MAPPED_TO_OUTPUT:
		hayward_log(HAYWARD_DEBUG, "Mapping input device %s to output %s",
			hayward_device->input_device->identifier, mapped_to_output);
		if (strcmp("*", mapped_to_output) == 0) {
			wlr_cursor_map_input_to_output(seat->cursor->cursor,
				hayward_device->input_device->wlr_device, NULL);
			wlr_cursor_map_input_to_region(seat->cursor->cursor,
				hayward_device->input_device->wlr_device, NULL);
			hayward_log(HAYWARD_DEBUG, "Reset output mapping");
			return;
		}
		struct hayward_output *output = output_by_name_or_id(mapped_to_output);
		if (!output) {
			hayward_log(HAYWARD_DEBUG, "Requested output %s for device %s isn't present",
				mapped_to_output, hayward_device->input_device->identifier);
			return;
		}
		wlr_cursor_map_input_to_output(seat->cursor->cursor,
			hayward_device->input_device->wlr_device, output->wlr_output);
		wlr_cursor_map_input_to_region(seat->cursor->cursor,
			hayward_device->input_device->wlr_device, NULL);
		hayward_log(HAYWARD_DEBUG,
			"Mapped to output %s", output->wlr_output->name);
		return;
	case MAPPED_TO_REGION:
		hayward_log(HAYWARD_DEBUG, "Mapping input device %s to %d,%d %dx%d",
			hayward_device->input_device->identifier,
			mapped_to_region->x, mapped_to_region->y,
			mapped_to_region->width, mapped_to_region->height);
		wlr_cursor_map_input_to_output(seat->cursor->cursor,
			hayward_device->input_device->wlr_device, NULL);
		wlr_cursor_map_input_to_region(seat->cursor->cursor,
			hayward_device->input_device->wlr_device, mapped_to_region);
		return;
	}
}

static void seat_configure_pointer(struct hayward_seat *seat,
		struct hayward_seat_device *hayward_device) {
	if ((seat->wlr_seat->capabilities & WL_SEAT_CAPABILITY_POINTER) == 0) {
		seat_configure_xcursor(seat);
	}
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		hayward_device->input_device->wlr_device);
	seat_apply_input_config(seat, hayward_device);
	wl_event_source_timer_update(
			seat->cursor->hide_source, cursor_get_timeout(seat->cursor));
}

static void seat_configure_keyboard(struct hayward_seat *seat,
		struct hayward_seat_device *seat_device) {
	if (!seat_device->keyboard) {
		hayward_keyboard_create(seat, seat_device);
	}
	hayward_keyboard_configure(seat_device->keyboard);
	wlr_seat_set_keyboard(seat->wlr_seat,
			seat_device->input_device->wlr_device->keyboard);

	// force notify reenter to pick up the new configuration.  This reuses
	// the current focused surface to avoid breaking input grabs.
	struct wlr_surface *surface = seat->wlr_seat->keyboard_state.focused_surface;
	if (surface) {
		wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
		seat_keyboard_notify_enter(seat, surface);
	}
}

static void seat_configure_switch(struct hayward_seat *seat,
		struct hayward_seat_device *seat_device) {
	if (!seat_device->switch_device) {
		hayward_switch_create(seat, seat_device);
	}
	seat_apply_input_config(seat, seat_device);
	hayward_switch_configure(seat_device->switch_device);
}

static void seat_configure_touch(struct hayward_seat *seat,
		struct hayward_seat_device *hayward_device) {
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		hayward_device->input_device->wlr_device);
	seat_apply_input_config(seat, hayward_device);
}

static void seat_configure_tablet_tool(struct hayward_seat *seat,
		struct hayward_seat_device *hayward_device) {
	if (!hayward_device->tablet) {
		hayward_device->tablet = hayward_tablet_create(seat, hayward_device);
	}
	hayward_configure_tablet(hayward_device->tablet);
	wlr_cursor_attach_input_device(seat->cursor->cursor,
		hayward_device->input_device->wlr_device);
	seat_apply_input_config(seat, hayward_device);
}

static void seat_configure_tablet_pad(struct hayward_seat *seat,
		struct hayward_seat_device *hayward_device) {
	if (!hayward_device->tablet_pad) {
		hayward_device->tablet_pad = hayward_tablet_pad_create(seat, hayward_device);
	}
	hayward_configure_tablet_pad(hayward_device->tablet_pad);
}

static struct hayward_seat_device *seat_get_device(struct hayward_seat *seat,
		struct hayward_input_device *input_device) {
	struct hayward_seat_device *seat_device = NULL;
	wl_list_for_each(seat_device, &seat->devices, link) {
		if (seat_device->input_device == input_device) {
			return seat_device;
		}
	}

	struct hayward_keyboard_group *group = NULL;
	wl_list_for_each(group, &seat->keyboard_groups, link) {
		if (group->seat_device->input_device == input_device) {
			return group->seat_device;
		}
	}

	return NULL;
}

void seat_configure_device(struct hayward_seat *seat,
		struct hayward_input_device *input_device) {
	struct hayward_seat_device *seat_device = seat_get_device(seat, input_device);
	if (!seat_device) {
		return;
	}

	switch (input_device->wlr_device->type) {
		case WLR_INPUT_DEVICE_POINTER:
			seat_configure_pointer(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_KEYBOARD:
			seat_configure_keyboard(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_SWITCH:
			seat_configure_switch(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			seat_configure_touch(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			seat_configure_tablet_tool(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			seat_configure_tablet_pad(seat, seat_device);
			break;
	}
}

void seat_reset_device(struct hayward_seat *seat,
		struct hayward_input_device *input_device) {
	struct hayward_seat_device *seat_device = seat_get_device(seat, input_device);
	if (!seat_device) {
		return;
	}

	switch (input_device->wlr_device->type) {
		case WLR_INPUT_DEVICE_POINTER:
			seat_reset_input_config(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_KEYBOARD:
			hayward_keyboard_disarm_key_repeat(seat_device->keyboard);
			hayward_keyboard_configure(seat_device->keyboard);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			seat_reset_input_config(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			seat_reset_input_config(seat, seat_device);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			hayward_log(HAYWARD_DEBUG, "TODO: reset tablet pad");
			break;
		case WLR_INPUT_DEVICE_SWITCH:
			hayward_log(HAYWARD_DEBUG, "TODO: reset switch device");
			break;
	}
}

void seat_add_device(struct hayward_seat *seat,
		struct hayward_input_device *input_device) {
	if (seat_get_device(seat, input_device)) {
		seat_configure_device(seat, input_device);
		return;
	}

	struct hayward_seat_device *seat_device =
		calloc(1, sizeof(struct hayward_seat_device));
	if (!seat_device) {
		hayward_log(HAYWARD_DEBUG, "could not allocate seat device");
		return;
	}

	hayward_log(HAYWARD_DEBUG, "adding device %s to seat %s",
		input_device->identifier, seat->wlr_seat->name);

	seat_device->hayward_seat = seat;
	seat_device->input_device = input_device;
	wl_list_insert(&seat->devices, &seat_device->link);

	seat_configure_device(seat, input_device);

	seat_update_capabilities(seat);
}

void seat_remove_device(struct hayward_seat *seat,
		struct hayward_input_device *input_device) {
	struct hayward_seat_device *seat_device = seat_get_device(seat, input_device);

	if (!seat_device) {
		return;
	}

	hayward_log(HAYWARD_DEBUG, "removing device %s from seat %s",
		input_device->identifier, seat->wlr_seat->name);

	seat_device_destroy(seat_device);

	seat_update_capabilities(seat);
}

static bool xcursor_manager_is_named(const struct wlr_xcursor_manager *manager,
		const char *name) {
	return (!manager->name && !name) ||
		(name && manager->name && strcmp(name, manager->name) == 0);
}

void seat_configure_xcursor(struct hayward_seat *seat) {
	unsigned cursor_size = 24;
	const char *cursor_theme = NULL;

	const struct seat_config *seat_config = seat_get_config(seat);
	if (!seat_config) {
		seat_config = seat_get_config_by_name("*");
	}
	if (seat_config) {
		cursor_size = seat_config->xcursor_theme.size;
		cursor_theme = seat_config->xcursor_theme.name;
	}

	if (seat == input_manager_get_default_seat()) {
		char cursor_size_fmt[16];
		snprintf(cursor_size_fmt, sizeof(cursor_size_fmt), "%u", cursor_size);
		setenv("XCURSOR_SIZE", cursor_size_fmt, 1);
		if (cursor_theme != NULL) {
			setenv("XCURSOR_THEME", cursor_theme, 1);
		}

#if HAVE_XWAYLAND
		if (server.xwayland.wlr_xwayland && (!server.xwayland.xcursor_manager ||
				!xcursor_manager_is_named(server.xwayland.xcursor_manager,
					cursor_theme) ||
				server.xwayland.xcursor_manager->size != cursor_size)) {

			wlr_xcursor_manager_destroy(server.xwayland.xcursor_manager);

			server.xwayland.xcursor_manager =
				wlr_xcursor_manager_create(cursor_theme, cursor_size);
			hayward_assert(server.xwayland.xcursor_manager,
						"Cannot create XCursor manager for theme");

			wlr_xcursor_manager_load(server.xwayland.xcursor_manager, 1);
			struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(
				server.xwayland.xcursor_manager, "left_ptr", 1);
			if (xcursor != NULL) {
				struct wlr_xcursor_image *image = xcursor->images[0];
				wlr_xwayland_set_cursor(
					server.xwayland.wlr_xwayland, image->buffer,
					image->width * 4, image->width, image->height,
					image->hotspot_x, image->hotspot_y);
			}
		}
#endif
	}

	/* Create xcursor manager if we don't have one already, or if the
	 * theme has changed */
	if (!seat->cursor->xcursor_manager ||
			!xcursor_manager_is_named(
				seat->cursor->xcursor_manager, cursor_theme) ||
			seat->cursor->xcursor_manager->size != cursor_size) {

		wlr_xcursor_manager_destroy(seat->cursor->xcursor_manager);
		seat->cursor->xcursor_manager =
			wlr_xcursor_manager_create(cursor_theme, cursor_size);
		if (!seat->cursor->xcursor_manager) {
			hayward_log(HAYWARD_ERROR,
				"Cannot create XCursor manager for theme '%s'", cursor_theme);
		}
	}

	for (int i = 0; i < root->outputs->length; ++i) {
		struct hayward_output *hayward_output = root->outputs->items[i];
		struct wlr_output *output = hayward_output->wlr_output;
		bool result =
			wlr_xcursor_manager_load(seat->cursor->xcursor_manager,
				output->scale);
		if (!result) {
			hayward_log(HAYWARD_ERROR,
				"Cannot load xcursor theme for output '%s' with scale %f",
				output->name, output->scale);
		}
	}

	// Reset the cursor so that we apply it to outputs that just appeared
	cursor_set_image(seat->cursor, NULL, NULL);
	cursor_set_image(seat->cursor, "left_ptr", NULL);
	wlr_cursor_warp(seat->cursor->cursor, NULL, seat->cursor->cursor->x,
		seat->cursor->cursor->y);
}

bool seat_is_input_allowed(struct hayward_seat *seat,
		struct wlr_surface *surface) {
	struct wl_client *client = wl_resource_get_client(surface->resource);
	return seat->exclusive_client == client ||
		(seat->exclusive_client == NULL && !server.session_lock.locked);
}

static void send_unfocus(struct hayward_window *container, void *data) {
	if (container->view) {
		view_set_activated(container->view, false);
	}
}

// TODO (hayward) deprecated.  Replace with `send_unfocus`.
// Unfocus the container and any children (eg. when leaving `focus parent`)
static void seat_send_unfocus(struct hayward_node *node, struct hayward_seat *seat) {
	hayward_cursor_constrain(seat->cursor, NULL);
	wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
	if (node->type == N_WORKSPACE) {
		workspace_for_each_window(node->hayward_workspace, send_unfocus, seat);
	} else if (node->type == N_WINDOW) {
		send_unfocus(node->hayward_window, seat);
	}
}

static int handle_urgent_timeout(void *data) {
	struct hayward_view *view = data;
	view_set_urgent(view, false);
	return 0;
}

/**
 * Moves a window to the top of the focus stack.
 *
 * Does not touch any other seat and so does not, as such, _focus_ the
 * window.  This function is intended as a building block for other functions
 * which do manipulate the focus.
 */
static void seat_set_active_window(struct hayward_seat *seat, struct hayward_window *window) {
	hayward_assert(window != NULL, "Expected non-null pointer");

	struct hayward_seat_window *seat_window = seat_window_from_window(seat, window);

	wl_list_remove(&seat_window->link);
	wl_list_insert(&seat->active_window_stack, &seat_window->link);

	node_set_dirty(&window->node);
	struct hayward_node *parent = node_get_parent(&window->node);
	if (parent) {
		node_set_dirty(parent);
	}
}

void seat_set_raw_focus(struct hayward_seat *seat, struct hayward_node *node) {
	hayward_assert(node->type == N_WINDOW, "Expected window");
	seat_set_active_window(seat, node->hayward_window);
}

static void seat_set_focus_internal(struct hayward_seat *seat, struct hayward_workspace *new_workspace, struct hayward_window *new_window) {
	hayward_assert(!new_window || new_window->pending.workspace == new_workspace, "Window workspace does not match expected");

 	if (wl_list_empty(&seat->active_workspace_stack)) {
		hayward_assert(new_workspace == NULL, "Can't focus non-existant workspace");
		hayward_assert(new_window == NULL, "Can't focus non-existant window");
		return;
	}

	hayward_assert(new_workspace != NULL, "Cannot focus null workspace");

	if (seat->focused_layer) {
		struct wlr_layer_surface_v1 *layer = seat->focused_layer;
		seat_set_focus_layer(seat, NULL);
		seat_set_focus_internal(seat, new_workspace, new_window);
		seat_set_focus_layer(seat, layer);
		return;
	}

	struct hayward_window *last_window = seat_get_focused_container(seat);
	struct hayward_workspace *last_workspace = seat_get_focused_workspace(seat);

	// Deny setting focus to a view which is hidden by a fullscreen container or global
	if (new_window && window_obstructing_fullscreen_window(new_window)) {
		return;
	}

	// Deny setting focus when an input grab or lockscreen is active
	if (new_window && !seat_is_input_allowed(seat, new_window->view->surface)) {
		return;
	}

	struct hayward_output *new_output = new_workspace->pending.output;
	struct hayward_workspace *new_output_last_workspace =
		new_output ? seat_get_active_workspace_for_output(seat, new_output) : NULL;

	if (new_workspace != last_workspace) {
		struct hayward_seat_workspace *seat_workspace = seat_workspace_from_workspace(seat, new_workspace);

		wl_list_remove(&seat_workspace->link);
		wl_list_insert(&seat->active_workspace_stack, &seat_workspace->link);

		if (new_output_last_workspace && new_workspace != new_output_last_workspace) {
			for (int i = 0; i < new_output_last_workspace->pending.floating->length; ++i) {
				struct hayward_window *floater =
					new_output_last_workspace->pending.floating->items[i];
				if (window_is_sticky(floater)) {
					window_detach(floater);
					workspace_add_floating(new_workspace, floater);
					--i;
				}
			}
		}

		if (last_workspace) {
			node_set_dirty(&last_workspace->node);
		}
		if (last_workspace && last_workspace->pending.output) {
			node_set_dirty(&last_workspace->pending.output->node);
		}

		node_set_dirty(&new_workspace->node);
		if (new_workspace->pending.output) {
			node_set_dirty(&new_workspace->pending.output->node);
		}
	}

	if (last_window && new_window != last_window) {
		// Unfocus the previous focus
		seat_send_unfocus(&last_window->node, seat);
		view_close_popups(last_window->view);

		node_set_dirty(&last_window->node);
		if (last_window->pending.parent) {
			node_set_dirty(&last_window->pending.parent->node);
		}
	}

	if (new_window && new_window != last_window) {
		// Move window to top of focus stack.
		struct hayward_seat_window *seat_window = seat_window_from_window(seat, new_window);
		wl_list_remove(&seat_window->link);
		wl_list_insert(&seat->active_window_stack, &seat_window->link);

		// Let the client know that it has focus.
		seat_send_focus(&new_window->node, seat);

		// If urgent, either unset the urgency or start a timer to unset it
		if (view_is_urgent(new_window->view) &&
				!new_window->view->urgent_timer) {
			struct hayward_view *view = new_window->view;
			if (last_workspace && last_workspace != new_workspace &&
					config->urgent_timeout > 0) {
				view->urgent_timer = wl_event_loop_add_timer(server.wl_event_loop,
						handle_urgent_timeout, view);
				if (view->urgent_timer) {
					wl_event_source_timer_update(view->urgent_timer,
							config->urgent_timeout);
				} else {
					hayward_log_errno(HAYWARD_ERROR, "Unable to create urgency timer");
					handle_urgent_timeout(view);
				}
			} else {
				view_set_urgent(view, false);
			}
		}

		node_set_dirty(&new_window->node);
		if (new_window->pending.parent) {
			node_set_dirty(&new_window->pending.parent->node);
		}
	}

	// Emit ipc events
	if (new_window != last_window) {
		ipc_event_window(new_window, "focus");
	}
	if (new_workspace != last_workspace) {
		ipc_event_workspace(last_workspace, new_workspace, "focus");
	}

	seat->has_focus = new_window ? true : false;

	if (new_output_last_workspace && new_output_last_workspace != new_workspace) {
		workspace_consider_destroy(new_output_last_workspace);
	}
	if (last_workspace && last_workspace != new_output_last_workspace && last_workspace != new_workspace) {
		workspace_consider_destroy(last_workspace);
	}

	if (config->smart_gaps && new_workspace) {
		// When smart gaps is on, gaps may change when the focus changes so
		// the workspace needs to be arranged
		arrange_workspace(new_workspace);
	}
}

void seat_set_focus(struct hayward_seat *seat, struct hayward_node *node) {
	if (node == NULL) {
		seat_set_focus_window(seat, NULL);
		return;
	}

	if (node->type == N_WINDOW) {
		seat_set_focus_window(seat, node->hayward_window);
		return;
	}

	if (node->type == N_WORKSPACE) {
		seat_set_focus_workspace(seat, node->hayward_workspace);
		return;
	}

	hayward_abort("Can't focus unknown node type");
}

void seat_clear_focus(struct hayward_seat *seat) {
	seat_set_focus(seat, NULL);
}

/**
 * Sets focus to a particular window.
 * If window is NULL, clears the window focus but leaves the current workspace
 * unchanged.
 *
 * If the focused window has been moved between workspaces, this function
 * should be called to patch up the workspace focus stack.
 */
void seat_set_focus_window(struct hayward_seat *seat, struct hayward_window *new_window) {
	struct hayward_workspace *new_workspace = new_window ? new_window->pending.workspace : seat_get_focused_workspace(seat);

	seat_set_focus_internal(seat, new_workspace, new_window);
}

/**
 * Sets focus to the active window on a workspace, or to the workspace itself
 * if empty.
 */
void seat_set_focus_workspace(struct hayward_seat *seat,
		struct hayward_workspace *new_workspace) {
	hayward_assert(new_workspace != NULL, "Can't set focus to null workspace");

	seat_set_focus_internal(seat, new_workspace, NULL);
}

void seat_set_focus_surface(struct hayward_seat *seat,
		struct wlr_surface *surface, bool unfocus) {
	if (seat->has_focus && unfocus) {
		struct hayward_node *focus = seat_get_focus(seat);
		seat_send_unfocus(focus, seat);
		seat->has_focus = false;
	}

	if (surface) {
		seat_keyboard_notify_enter(seat, surface);
	} else {
		wlr_seat_keyboard_notify_clear_focus(seat->wlr_seat);
	}

	hayward_input_method_relay_set_focus(&seat->im_relay, surface);
	seat_tablet_pads_notify_enter(seat, surface);
}

void seat_set_focus_layer(struct hayward_seat *seat,
		struct wlr_layer_surface_v1 *layer) {
	if (!layer && seat->focused_layer) {
		seat->focused_layer = NULL;
		struct hayward_node *previous = seat_get_focus_inactive(seat, &root->node);
		if (previous) {
			// Hack to get seat to re-focus the return value of get_focus
			seat_set_focus(seat, NULL);
			seat_set_focus(seat, previous);
		}
		return;
	} else if (!layer || seat->focused_layer == layer) {
		return;
	}
	assert(layer->mapped);
	seat_set_focus_surface(seat, layer->surface, true);
	if (layer->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
		seat->focused_layer = layer;
	}
}

void seat_set_exclusive_client(struct hayward_seat *seat,
		struct wl_client *client) {
	if (!client) {
		seat->exclusive_client = client;
		// Triggers a refocus of the topmost surface layer if necessary
		// TODO: Make layer surface focus per-output based on cursor position
		for (int i = 0; i < root->outputs->length; ++i) {
			struct hayward_output *output = root->outputs->items[i];
			arrange_layers(output);
		}
		return;
	}
	if (seat->focused_layer) {
		if (wl_resource_get_client(seat->focused_layer->resource) != client) {
			seat_set_focus_layer(seat, NULL);
		}
	}
	if (seat->has_focus) {
		struct hayward_node *focus = seat_get_focus(seat);
		if (node_is_view(focus) && wl_resource_get_client(
					focus->hayward_window->view->surface->resource) != client) {
			seat_set_focus(seat, NULL);
		}
	}
	if (seat->wlr_seat->pointer_state.focused_client) {
		if (seat->wlr_seat->pointer_state.focused_client->client != client) {
			wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
		}
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	struct wlr_touch_point *point;
	wl_list_for_each(point, &seat->wlr_seat->touch_state.touch_points, link) {
		if (point->client->client != client) {
			wlr_seat_touch_point_clear_focus(seat->wlr_seat,
					now.tv_nsec / 1000, point->touch_id);
		}
	}
	seat->exclusive_client = client;
}

struct hayward_workspace *seat_get_active_workspace_for_output(struct hayward_seat *seat, struct hayward_output *output) {
	struct hayward_seat_workspace *current;
	wl_list_for_each(current, &seat->active_workspace_stack, link) {
		struct hayward_workspace *workspace = current->workspace;

		if (workspace->pending.output == NULL) {
			continue;
		}

		if (workspace->pending.output != output) {
			continue;
		}

		return workspace;
	}

	return NULL;
}

struct hayward_window *seat_get_active_window_for_column(struct hayward_seat *seat, struct hayward_column *column) {
	struct hayward_seat_window *current;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		struct hayward_window *window = current->window;

		if (window->pending.parent != column) {
			continue;
		}

		return window;
	}

	return NULL;
}

struct hayward_window *seat_get_active_tiling_window_for_workspace(struct hayward_seat *seat, struct hayward_workspace *workspace) {
	if (!workspace->pending.tiling->length) {
		return NULL;
	}

	struct hayward_seat_window *current;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		struct hayward_window *window = current->window;

		if (window->pending.workspace != workspace) {
			continue;
		}

		if (!window_is_tiling(window)) {
			continue;
		}

		return window;
	}

	return NULL;
}

struct hayward_window *seat_get_active_floating_window_for_workspace(struct hayward_seat *seat, struct hayward_workspace *workspace) {
	if (!workspace->pending.floating->length) {
		return NULL;
	}

	struct hayward_seat_window *current;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		struct hayward_window *window = current->window;

		if (window->pending.workspace != workspace) {
			continue;
		}

		if (!window_is_floating(window)) {
			continue;
		}

		return window;
	}

	return NULL;

}

struct hayward_window *seat_get_active_window_for_workspace(struct hayward_seat *seat, struct hayward_workspace *workspace) {
	if (!workspace->pending.tiling->length) {
		return NULL;
	}

	struct hayward_seat_window *current;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		struct hayward_window *window = current->window;

		if (window->pending.workspace != workspace) {
			continue;
		}

		return window;
	}

	return NULL;
}

// TODO (hayward) deprecated.
struct hayward_node *seat_get_focus_inactive(struct hayward_seat *seat,
		struct hayward_node *node) {
	if (node_is_view(node)) {
		return node;
	}
	struct hayward_seat_window *current;
	wl_list_for_each(current, &seat->active_window_stack, link) {
		if (node_has_ancestor(&current->window->node, node)) {
			return &current->window->node;
		}
	}
	if (node->type == N_WORKSPACE) {
		return node;
	}
	return NULL;
}

// TODO (hayward) deprecated.
struct hayward_window *seat_get_focus_inactive_tiling(struct hayward_seat *seat,
		struct hayward_workspace *workspace) {
	return seat_get_active_tiling_window_for_workspace(seat, workspace);
}

// TODO (hayward) deprecated.
struct hayward_window *seat_get_focus_inactive_floating(struct hayward_seat *seat,
		struct hayward_workspace *workspace) {
	return seat_get_active_floating_window_for_workspace(seat, workspace);
}

// TODO (hayward) deprecated.
struct hayward_node *seat_get_active_tiling_child(struct hayward_seat *seat,
		struct hayward_node *parent) {
	struct hayward_window *window = NULL;

	switch (parent->type) {
	case N_WORKSPACE:
		window = seat_get_active_tiling_window_for_workspace(seat, parent->hayward_workspace);
		break;

	case N_COLUMN:
		window = seat_get_active_window_for_column(seat, parent->hayward_column);
		break;

	case N_WINDOW:
		window = parent->hayward_window;
		break;

	default:
		hayward_assert(false, "Unexpected node type");
	}
	return window != NULL ? &window->node : NULL;
}

struct hayward_node *seat_get_focus(struct hayward_seat *seat) {
	if (!seat->has_focus) {
		return NULL;
	}
	hayward_assert(!wl_list_empty(&seat->active_window_stack),
			"active_window_stack is empty, but has_focus is true");
	struct hayward_seat_window *current =
		wl_container_of(seat->active_window_stack.next, current, link);
	// TODO (hayward) should just return window.
	return &current->window->node;
}

struct hayward_workspace *seat_get_focused_workspace(struct hayward_seat *seat) {
	if (wl_list_empty(&seat->active_workspace_stack)) {
		return NULL;
	}

	struct hayward_seat_workspace *seat_workspace =
		wl_container_of(seat->active_workspace_stack.next, seat_workspace, link);

	return seat_workspace->workspace;
}

struct hayward_window *seat_get_focused_container(struct hayward_seat *seat) {
	struct hayward_node *focus = seat_get_focus(seat);
	if (focus && (focus->type == N_WINDOW)) {
		return focus->hayward_window;
	}
	return NULL;
}

void seat_apply_config(struct hayward_seat *seat,
		struct seat_config *seat_config) {
	struct hayward_seat_device *seat_device = NULL;

	if (!seat_config) {
		return;
	}

	seat->idle_inhibit_sources = seat_config->idle_inhibit_sources;
	seat->idle_wake_sources = seat_config->idle_wake_sources;

	wl_list_for_each(seat_device, &seat->devices, link) {
		seat_configure_device(seat, seat_device->input_device);
		cursor_handle_activity_from_device(seat->cursor,
			seat_device->input_device->wlr_device);
	}
}

struct seat_config *seat_get_config(struct hayward_seat *seat) {
	struct seat_config *seat_config = NULL;
	for (int i = 0; i < config->seat_configs->length; ++i ) {
		seat_config = config->seat_configs->items[i];
		if (strcmp(seat->wlr_seat->name, seat_config->name) == 0) {
			return seat_config;
		}
	}

	return NULL;
}

struct seat_config *seat_get_config_by_name(const char *name) {
	struct seat_config *seat_config = NULL;
	for (int i = 0; i < config->seat_configs->length; ++i ) {
		seat_config = config->seat_configs->items[i];
		if (strcmp(name, seat_config->name) == 0) {
			return seat_config;
		}
	}

	return NULL;
}

void seat_pointer_notify_button(struct hayward_seat *seat, uint32_t time_msec,
		uint32_t button, enum wlr_button_state state) {
	seat->last_button_serial = wlr_seat_pointer_notify_button(seat->wlr_seat,
			time_msec, button, state);
}

void seat_consider_warp_to_focus(struct hayward_seat *seat) {
	struct hayward_node *focus = seat_get_focus(seat);
	if (config->mouse_warping == WARP_NO || !focus) {
		return;
	}
	if (config->mouse_warping == WARP_OUTPUT) {
		struct hayward_output *output = node_get_output(focus);
		if (output) {
			struct wlr_box box;
			output_get_box(output, &box);
			if (wlr_box_contains_point(&box,
						seat->cursor->cursor->x, seat->cursor->cursor->y)) {
				return;
			}
		}
	}

	if (focus->type == N_WINDOW) {
		cursor_warp_to_container(seat->cursor, focus->hayward_window, false);
	} else {
		cursor_warp_to_workspace(seat->cursor, focus->hayward_workspace);
	}
}

void seatop_unref(struct hayward_seat *seat, struct hayward_window *container) {
	if (seat->seatop_impl->unref) {
		seat->seatop_impl->unref(seat, container);
	}
}

void seatop_button(struct hayward_seat *seat, uint32_t time_msec,
		struct wlr_input_device *device, uint32_t button,
		enum wlr_button_state state) {
	if (seat->seatop_impl->button) {
		seat->seatop_impl->button(seat, time_msec, device, button, state);
	}
}

void seatop_pointer_motion(struct hayward_seat *seat, uint32_t time_msec) {
	if (seat->seatop_impl->pointer_motion) {
		seat->seatop_impl->pointer_motion(seat, time_msec);
	}
}

void seatop_pointer_axis(struct hayward_seat *seat,
		struct wlr_pointer_axis_event *event) {
	if (seat->seatop_impl->pointer_axis) {
		seat->seatop_impl->pointer_axis(seat, event);
	}
}

void seatop_tablet_tool_tip(struct hayward_seat *seat,
		struct hayward_tablet_tool *tool, uint32_t time_msec,
		enum wlr_tablet_tool_tip_state state) {
	if (seat->seatop_impl->tablet_tool_tip) {
		seat->seatop_impl->tablet_tool_tip(seat, tool, time_msec, state);
	}
}

void seatop_tablet_tool_motion(struct hayward_seat *seat,
		struct hayward_tablet_tool *tool, uint32_t time_msec) {
	if (seat->seatop_impl->tablet_tool_motion) {
		seat->seatop_impl->tablet_tool_motion(seat, tool, time_msec);
	} else {
		seatop_pointer_motion(seat, time_msec);
	}
}

void seatop_rebase(struct hayward_seat *seat, uint32_t time_msec) {
	if (seat->seatop_impl->rebase) {
		seat->seatop_impl->rebase(seat, time_msec);
	}
}

void seatop_end(struct hayward_seat *seat) {
	if (seat->seatop_impl && seat->seatop_impl->end) {
		seat->seatop_impl->end(seat);
	}
	free(seat->seatop_data);
	seat->seatop_data = NULL;
	seat->seatop_impl = NULL;
}

void seatop_render(struct hayward_seat *seat, struct hayward_output *output,
		pixman_region32_t *damage) {
	if (seat->seatop_impl->render) {
		seat->seatop_impl->render(seat, output, damage);
	}
}

bool seatop_allows_set_cursor(struct hayward_seat *seat) {
	return seat->seatop_impl->allow_set_cursor;
}

struct hayward_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_surface(
		const struct hayward_seat *seat,
		const struct wlr_surface *surface) {
	struct hayward_keyboard_shortcuts_inhibitor *hayward_inhibitor = NULL;
	wl_list_for_each(hayward_inhibitor, &seat->keyboard_shortcuts_inhibitors, link) {
		if (hayward_inhibitor->inhibitor->surface == surface) {
			return hayward_inhibitor;
		}
	}

	return NULL;
}

struct hayward_keyboard_shortcuts_inhibitor *
keyboard_shortcuts_inhibitor_get_for_focused_surface(
		const struct hayward_seat *seat) {
	return keyboard_shortcuts_inhibitor_get_for_surface(seat,
		seat->wlr_seat->keyboard_state.focused_surface);
}
