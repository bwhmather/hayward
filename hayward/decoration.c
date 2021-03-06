#include <stdlib.h>
#include "hayward/decoration.h"
#include "hayward/desktop/transaction.h"
#include "hayward/server.h"
#include "hayward/tree/arrange.h"
#include "hayward/tree/view.h"
#include "log.h"

static void server_decoration_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct hayward_server_decoration *deco =
		wl_container_of(listener, deco, destroy);
	wl_list_remove(&deco->destroy.link);
	wl_list_remove(&deco->mode.link);
	wl_list_remove(&deco->link);
	free(deco);
}

static void server_decoration_handle_mode(struct wl_listener *listener,
		void *data) {
	struct hayward_server_decoration *deco =
		wl_container_of(listener, deco, mode);
	struct hayward_view *view =
		view_from_wlr_surface(deco->wlr_server_decoration->surface);
	if (view == NULL || view->surface != deco->wlr_server_decoration->surface) {
		return;
	}

	bool csd = deco->wlr_server_decoration->mode ==
			WLR_SERVER_DECORATION_MANAGER_MODE_CLIENT;
	view_update_csd_from_client(view, csd);

	arrange_window(view->window);
	transaction_commit_dirty();
}

void handle_server_decoration(struct wl_listener *listener, void *data) {
	struct wlr_server_decoration *wlr_deco = data;

	struct hayward_server_decoration *deco = calloc(1, sizeof(*deco));
	if (deco == NULL) {
		return;
	}

	deco->wlr_server_decoration = wlr_deco;

	wl_signal_add(&wlr_deco->events.destroy, &deco->destroy);
	deco->destroy.notify = server_decoration_handle_destroy;

	wl_signal_add(&wlr_deco->events.mode, &deco->mode);
	deco->mode.notify = server_decoration_handle_mode;

	wl_list_insert(&server.decorations, &deco->link);
}

struct hayward_server_decoration *decoration_from_surface(
		struct wlr_surface *surface) {
	struct hayward_server_decoration *deco;
	wl_list_for_each(deco, &server.decorations, link) {
		if (deco->wlr_server_decoration->surface == surface) {
			return deco;
		}
	}
	return NULL;
}
