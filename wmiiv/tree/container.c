#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/render/drm_format_set.h>
#include "linux-dmabuf-unstable-v1-protocol.h"
#include "cairo_util.h"
#include "pango.h"
#include "wmiiv/config.h"
#include "wmiiv/desktop.h"
#include "wmiiv/desktop/transaction.h"
#include "wmiiv/input/input-manager.h"
#include "wmiiv/input/seat.h"
#include "wmiiv/ipc-server.h"
#include "wmiiv/output.h"
#include "wmiiv/server.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/workspace.h"
#include "wmiiv/xdg_decoration.h"
#include "list.h"
#include "log.h"
#include "stringop.h"

bool container_is_column(struct wmiiv_container* container) {
	return container->view == NULL;
}

bool container_is_window(struct wmiiv_container* container) {
	return container->view != NULL;
}

void container_destroy(struct wmiiv_container *container) {
	if (!wmiiv_assert(container->node.destroying,
				"Tried to free container which wasn't marked as destroying")) {
		return;
	}
	if (!wmiiv_assert(container->node.ntxnrefs == 0, "Tried to free container "
				"which is still referenced by transactions")) {
		return;
	}
	free(container->title);
	free(container->formatted_title);
	wlr_texture_destroy(container->title_focused);
	wlr_texture_destroy(container->title_focused_inactive);
	wlr_texture_destroy(container->title_unfocused);
	wlr_texture_destroy(container->title_urgent);
	wlr_texture_destroy(container->title_focused_tab_title);
	list_free(container->pending.children);
	list_free(container->current.children);
	list_free(container->outputs);

	list_free_items_and_destroy(container->marks);
	wlr_texture_destroy(container->marks_focused);
	wlr_texture_destroy(container->marks_focused_inactive);
	wlr_texture_destroy(container->marks_unfocused);
	wlr_texture_destroy(container->marks_urgent);
	wlr_texture_destroy(container->marks_focused_tab_title);

	if (container->view && container->view->container == container) {
		container->view->container = NULL;
		if (container->view->destroying) {
			view_destroy(container->view);
		}
	}

	free(container);
}

void container_begin_destroy(struct wmiiv_container *container) {
	if (container->view) {
		ipc_event_window(container, "close");
	}
	// The workspace must have the fullscreen pointer cleared so that the
	// seat code can find an appropriate new focus.
	if (container->pending.fullscreen_mode == FULLSCREEN_WORKSPACE && container->pending.workspace) {
		container->pending.workspace->fullscreen = NULL;
	}

	wl_signal_emit(&container->node.events.destroy, &container->node);

	container_end_mouse_operation(container);

	container->node.destroying = true;
	node_set_dirty(&container->node);

	if (container->pending.fullscreen_mode == FULLSCREEN_GLOBAL) {
		window_fullscreen_disable(container);
	}

	if (container->pending.parent || container->pending.workspace) {
		if (container_is_window(container)) {
			window_detach(container);
		} else {
			column_detach(container);
		}
	}
}

/**
 * Return the output which will be used for scale purposes.
 * This is the most recently entered output.
 */
struct wmiiv_output *container_get_effective_output(struct wmiiv_container *container) {
	if (container->outputs->length == 0) {
		return NULL;
	}
	return container->outputs->items[container->outputs->length - 1];
}

static bool find_urgent_iterator(struct wmiiv_container *container, void *data) {
	return container->view && view_is_urgent(container->view);
}

bool container_has_urgent_child(struct wmiiv_container *container) {
	return column_find_child(container, find_urgent_iterator, NULL);
}

void container_end_mouse_operation(struct wmiiv_container *container) {
	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		seatop_unref(seat, container);
	}
}

struct wmiiv_container *container_toplevel_ancestor(
		struct wmiiv_container *container) {
	while (container->pending.parent) {
		container = container->pending.parent;
	}

	return container;
}

static void surface_send_enter_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_enter(surface, wlr_output);
}

static void surface_send_leave_iterator(struct wlr_surface *surface,
		int x, int y, void *data) {
	struct wlr_output *wlr_output = data;
	wlr_surface_send_leave(surface, wlr_output);
}

void container_discover_outputs(struct wmiiv_container *container) {
	struct wlr_box container_box = {
		.x = container->current.x,
		.y = container->current.y,
		.width = container->current.width,
		.height = container->current.height,
	};
	struct wmiiv_output *old_output = container_get_effective_output(container);

	for (int i = 0; i < root->outputs->length; ++i) {
		struct wmiiv_output *output = root->outputs->items[i];
		struct wlr_box output_box;
		output_get_box(output, &output_box);
		struct wlr_box intersection;
		bool intersects =
			wlr_box_intersection(&intersection, &container_box, &output_box);
		int index = list_find(container->outputs, output);

		if (intersects && index == -1) {
			// Send enter
			wmiiv_log(WMIIV_DEBUG, "Container %p entered output %p", container, output);
			if (container->view) {
				view_for_each_surface(container->view,
						surface_send_enter_iterator, output->wlr_output);
				if (container->view->foreign_toplevel) {
					wlr_foreign_toplevel_handle_v1_output_enter(
							container->view->foreign_toplevel, output->wlr_output);
				}
			}
			list_add(container->outputs, output);
		} else if (!intersects && index != -1) {
			// Send leave
			wmiiv_log(WMIIV_DEBUG, "Container %p left output %p", container, output);
			if (container->view) {
				view_for_each_surface(container->view,
					surface_send_leave_iterator, output->wlr_output);
				if (container->view->foreign_toplevel) {
					wlr_foreign_toplevel_handle_v1_output_leave(
							container->view->foreign_toplevel, output->wlr_output);
				}
			}
			list_del(container->outputs, index);
		}
	}
	struct wmiiv_output *new_output = container_get_effective_output(container);
	double old_scale = old_output && old_output->enabled ?
		old_output->wlr_output->scale : -1;
	double new_scale = new_output ? new_output->wlr_output->scale : -1;
	if (old_scale != new_scale) {
		if (container_is_window(container)) {
			window_update_title_textures(container);
			window_update_marks_textures(container);
		}
	}
}

enum wmiiv_container_layout container_parent_layout(struct wmiiv_container *container) {
	if (container_is_window(container)) {
		if (container->pending.parent) {
			return container->pending.parent->pending.layout;
		}
		return L_NONE;
	} else {
		// TODO (wmiiv) There should be no need for this branch.  Can
		// probably all be moved to window module.
		if (container->pending.parent) {
			return container->pending.parent->pending.layout;
		}
		if (container->pending.workspace) {
			return L_HORIZ;
		}
		return L_NONE;
	}
}

enum wmiiv_container_layout container_current_parent_layout(
		struct wmiiv_container *container) {
	if (container->current.parent) {
		return container->current.parent->current.layout;
	}
	// TODO (wmiiv) workspace default layout.
	return L_HORIZ;
}


