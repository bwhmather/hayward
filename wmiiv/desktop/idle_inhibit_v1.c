#include <stdlib.h>
#include <wlr/types/wlr_idle.h>
#include "log.h"
#include "wmiiv/desktop/idle_inhibit_v1.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/server.h"


static void destroy_inhibitor(struct wmiiv_idle_inhibitor_v1 *inhibitor) {
	wl_list_remove(&inhibitor->link);
	wl_list_remove(&inhibitor->destroy.link);
	wmiiv_idle_inhibit_v1_check_active(inhibitor->manager);
	free(inhibitor);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct wmiiv_idle_inhibitor_v1 *inhibitor =
		wl_container_of(listener, inhibitor, destroy);
	wmiiv_log(WMIIV_DEBUG, "WMiiv idle inhibitor destroyed");
	destroy_inhibitor(inhibitor);
}

void handle_idle_inhibitor_v1(struct wl_listener *listener, void *data) {
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
	struct wmiiv_idle_inhibit_manager_v1 *manager =
		wl_container_of(listener, manager, new_idle_inhibitor_v1);
	wmiiv_log(WMIIV_DEBUG, "New wmiiv idle inhibitor");

	struct wmiiv_idle_inhibitor_v1 *inhibitor =
		calloc(1, sizeof(struct wmiiv_idle_inhibitor_v1));
	if (!inhibitor) {
		return;
	}

	inhibitor->manager = manager;
	inhibitor->mode = INHIBIT_IDLE_APPLICATION;
	inhibitor->wlr_inhibitor = wlr_inhibitor;
	wl_list_insert(&manager->inhibitors, &inhibitor->link);

	inhibitor->destroy.notify = handle_destroy;
	wl_signal_add(&wlr_inhibitor->events.destroy, &inhibitor->destroy);

	wmiiv_idle_inhibit_v1_check_active(manager);
}

void wmiiv_idle_inhibit_v1_user_inhibitor_register(struct wmiiv_view *view,
		enum wmiiv_idle_inhibit_mode mode) {
	struct wmiiv_idle_inhibitor_v1 *inhibitor =
		calloc(1, sizeof(struct wmiiv_idle_inhibitor_v1));
	if (!inhibitor) {
		return;
	}

	inhibitor->manager = server.idle_inhibit_manager_v1;
	inhibitor->mode = mode;
	inhibitor->view = view;
	wl_list_insert(&inhibitor->manager->inhibitors, &inhibitor->link);

	inhibitor->destroy.notify = handle_destroy;
	wl_signal_add(&view->events.unmap, &inhibitor->destroy);

	wmiiv_idle_inhibit_v1_check_active(inhibitor->manager);
}

struct wmiiv_idle_inhibitor_v1 *wmiiv_idle_inhibit_v1_user_inhibitor_for_view(
		struct wmiiv_view *view) {
	struct wmiiv_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server.idle_inhibit_manager_v1->inhibitors,
			link) {
		if (inhibitor->mode != INHIBIT_IDLE_APPLICATION &&
				inhibitor->view == view) {
			return inhibitor;
		}
	}
	return NULL;
}

struct wmiiv_idle_inhibitor_v1 *wmiiv_idle_inhibit_v1_application_inhibitor_for_view(
		struct wmiiv_view *view) {
	struct wmiiv_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &server.idle_inhibit_manager_v1->inhibitors,
			link) {
		if (inhibitor->mode == INHIBIT_IDLE_APPLICATION &&
				view_from_wlr_surface(inhibitor->wlr_inhibitor->surface) == view) {
			return inhibitor;
		}
	}
	return NULL;
}

void wmiiv_idle_inhibit_v1_user_inhibitor_destroy(
		struct wmiiv_idle_inhibitor_v1 *inhibitor) {
	if (!inhibitor) {
		return;
	}
	if (!wmiiv_assert(inhibitor->mode != INHIBIT_IDLE_APPLICATION,
				"User should not be able to destroy application inhibitor")) {
		return;
	}
	destroy_inhibitor(inhibitor);
}

bool wmiiv_idle_inhibit_v1_is_active(struct wmiiv_idle_inhibitor_v1 *inhibitor) {
	switch (inhibitor->mode) {
	case INHIBIT_IDLE_APPLICATION:;
		// If there is no view associated with the inhibitor, assume visible
		struct wmiiv_view *view = view_from_wlr_surface(inhibitor->wlr_inhibitor->surface);
		return !view || !view->container || view_is_visible(view);
	case INHIBIT_IDLE_FOCUS:;
		struct wmiiv_seat *seat = NULL;
		wl_list_for_each(seat, &server.input->seats, link) {
			struct wmiiv_container *con = seat_get_focused_container(seat);
			if (con && con->view && con->view == inhibitor->view) {
				return true;
			}
		}
		return false;
	case INHIBIT_IDLE_FULLSCREEN:
		return inhibitor->view->container &&
			window_is_fullscreen(inhibitor->view->container) &&
			view_is_visible(inhibitor->view);
	case INHIBIT_IDLE_OPEN:
		// Inhibitor is destroyed on unmap so it must be open/mapped
		return true;
	case INHIBIT_IDLE_VISIBLE:
		return view_is_visible(inhibitor->view);
	}
	return false;
}

void wmiiv_idle_inhibit_v1_check_active(
		struct wmiiv_idle_inhibit_manager_v1 *manager) {
	struct wmiiv_idle_inhibitor_v1 *inhibitor;
	bool inhibited = false;
	wl_list_for_each(inhibitor, &manager->inhibitors, link) {
		if ((inhibited = wmiiv_idle_inhibit_v1_is_active(inhibitor))) {
			break;
		}
	}
	wlr_idle_set_enabled(manager->idle, NULL, !inhibited);
}

struct wmiiv_idle_inhibit_manager_v1 *wmiiv_idle_inhibit_manager_v1_create(
		struct wl_display *wl_display, struct wlr_idle *idle) {
	struct wmiiv_idle_inhibit_manager_v1 *manager =
		calloc(1, sizeof(struct wmiiv_idle_inhibit_manager_v1));
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
