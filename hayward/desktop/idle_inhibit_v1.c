#include <stdlib.h>
#include <wlr/types/wlr_idle.h>
#include "log.h"
#include "hayward/desktop/idle_inhibit_v1.h"
#include "hayward/input/seat.h"
#include "hayward/tree/window.h"
#include "hayward/tree/view.h"
#include "hayward/server.h"


static void destroy_inhibitor(struct hayward_idle_inhibitor_v1 *inhibitor) {
	wl_list_remove(&inhibitor->link);
	wl_list_remove(&inhibitor->destroy.link);
	hayward_idle_inhibit_v1_check_active(inhibitor->manager);
	free(inhibitor);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct hayward_idle_inhibitor_v1 *inhibitor =
		wl_container_of(listener, inhibitor, destroy);
	hayward_log(HAYWARD_DEBUG, "Hayward idle inhibitor destroyed");
	destroy_inhibitor(inhibitor);
}

void handle_idle_inhibitor_v1(struct wl_listener *listener, void *data) {
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
	struct hayward_idle_inhibit_manager_v1 *manager =
		wl_container_of(listener, manager, new_idle_inhibitor_v1);
	hayward_log(HAYWARD_DEBUG, "New hayward idle inhibitor");

	struct hayward_idle_inhibitor_v1 *inhibitor =
		calloc(1, sizeof(struct hayward_idle_inhibitor_v1));
	if (!inhibitor) {
		return;
	}

	inhibitor->manager = manager;
	inhibitor->mode = INHIBIT_IDLE_APPLICATION;
	inhibitor->wlr_inhibitor = wlr_inhibitor;
	wl_list_insert(&manager->inhibitors, &inhibitor->link);

	inhibitor->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

	hayward_idle_inhibit_v1_check_active(manager);
}

void hayward_idle_inhibit_v1_user_inhibitor_register(struct hayward_view *view,
		enum hayward_idle_inhibit_mode mode) {
	struct hayward_idle_inhibitor_v1 *inhibitor =
		calloc(1, sizeof(struct hayward_idle_inhibitor_v1));
	if (!inhibitor) {
		return;
	}

	inhibitor->manager = server.idle_inhibit_manager_v1;
	inhibitor->mode = mode;
	inhibitor->view = view;
	wl_list_insert(&inhibitor->manager->inhibitors, &inhibitor->link);

	inhibitor->destroy.notify = handle_destroy;
	wl_signal_add(&view->events.unmap, &inhibitor->destroy);

	hayward_idle_inhibit_v1_check_active(inhibitor->manager);
}

struct hayward_idle_inhibitor_v1 *hayward_idle_inhibit_v1_user_inhibitor_for_view(
		struct hayward_view *view) {
	struct hayward_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server.idle_inhibit_manager_v1->inhibitors,
			link) {
		if (inhibitor->mode != INHIBIT_IDLE_APPLICATION &&
				inhibitor->view == view) {
			return inhibitor;
		}
	}
	return NULL;
}

struct hayward_idle_inhibitor_v1 *hayward_idle_inhibit_v1_application_inhibitor_for_view(
		struct hayward_view *view) {
	struct hayward_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server.idle_inhibit_manager_v1->inhibitors,
			link) {
		if (inhibitor->mode == INHIBIT_IDLE_APPLICATION &&
				view_from_wlr_surface(inhibitor->wlr_inhibitor->surface) == view) {
			return inhibitor;
		}
	}
	return NULL;
}

void hayward_idle_inhibit_v1_user_inhibitor_destroy(
		struct hayward_idle_inhibitor_v1 *inhibitor) {
	if (!inhibitor) {
		return;
	}
	hayward_assert(inhibitor->mode != INHIBIT_IDLE_APPLICATION,
				"User should not be able to destroy application inhibitor");
	destroy_inhibitor(inhibitor);
}

bool hayward_idle_inhibit_v1_is_active(struct hayward_idle_inhibitor_v1 *inhibitor) {
	switch (inhibitor->mode) {
	case INHIBIT_IDLE_APPLICATION:;
		// If there is no view associated with the inhibitor, assume visible
		struct hayward_view *view = view_from_wlr_surface(inhibitor->wlr_inhibitor->surface);
		return !view || !view->window || view_is_visible(view);
	case INHIBIT_IDLE_FOCUS:;
		struct hayward_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct hayward_window *container = seat_get_focused_container(seat);
			if (container && container->view && container->view == inhibitor->view) {
				return true;
			}
		}
		return false;
	case INHIBIT_IDLE_FULLSCREEN:
		return inhibitor->view->window &&
			window_is_fullscreen(inhibitor->view->window) &&
			view_is_visible(inhibitor->view);
	case INHIBIT_IDLE_OPEN:
		// Inhibitor is destroyed on unmap so it must be open/mapped
		return true;
	case INHIBIT_IDLE_VISIBLE:
		return view_is_visible(inhibitor->view);
	}
	return false;
}

void hayward_idle_inhibit_v1_check_active(
		struct hayward_idle_inhibit_manager_v1 *manager) {
	struct hayward_idle_inhibitor_v1 *inhibitor;
	bool inhibited = false;
	wl_list_for_each(inhibitor, &manager->inhibitors, link) {
		if ((inhibited = hayward_idle_inhibit_v1_is_active(inhibitor))) {
			break;
		}
	}
	wlr_idle_set_enabled(manager->idle, NULL, !inhibited);
}

struct hayward_idle_inhibit_manager_v1 *hayward_idle_inhibit_manager_v1_create(
		struct wl_display *wl_display, struct wlr_idle *idle) {
	struct hayward_idle_inhibit_manager_v1 *manager =
		calloc(1, sizeof(struct hayward_idle_inhibit_manager_v1));
	if (!manager) {
		return NULL;
	}

	manager->wlr_manager = wlr_idle_inhibit_v1_create(wl_display);
	if (!manager->wlr_manager) {
		free(manager);
		return NULL;
	}
	manager->idle = idle;
	wl_signal_add(&manager->wlr_manager->events.new_inhibitor,
		&manager->new_idle_inhibitor_v1);
	manager->new_idle_inhibitor_v1.notify = handle_idle_inhibitor_v1;
	wl_list_init(&manager->inhibitors);

	return manager;
}
