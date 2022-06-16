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
		container_fullscreen_disable(container);
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

void container_get_box(struct wmiiv_container *container, struct wlr_box *box) {
	box->x = container->pending.x;
	box->y = container->pending.y;
	box->width = container->pending.width;
	box->height = container->pending.height;
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

static bool devid_from_fd(int fd, dev_t *devid) {
	struct stat stat;
	if (fstat(fd, &stat) != 0) {
		wmiiv_log_errno(WMIIV_ERROR, "fstat failed");
		return false;
	}
	*devid = stat.st_rdev;
	return true;
}

static void set_fullscreen(struct wmiiv_container *container, bool enable) {
	if (!container->view) {
		return;
	}
	if (container->view->impl->set_fullscreen) {
		container->view->impl->set_fullscreen(container->view, enable);
		if (container->view->foreign_toplevel) {
			wlr_foreign_toplevel_handle_v1_set_fullscreen(
				container->view->foreign_toplevel, enable);
		}
	}

	if (!server.linux_dmabuf_v1 || !container->view->surface) {
		return;
	}
	if (!enable) {
		wlr_linux_dmabuf_v1_set_surface_feedback(server.linux_dmabuf_v1,
			container->view->surface, NULL);
		return;
	}

	if (!container->pending.workspace || !container->pending.workspace->output) {
		return;
	}

	struct wmiiv_output *output = container->pending.workspace->output;
	struct wlr_output *wlr_output = output->wlr_output;

	// TODO: add wlroots helpers for all of this stuff

	const struct wlr_drm_format_set *renderer_formats =
		wlr_renderer_get_dmabuf_texture_formats(server.renderer);
	assert(renderer_formats);

	int renderer_drm_fd = wlr_renderer_get_drm_fd(server.renderer);
	int backend_drm_fd = wlr_backend_get_drm_fd(wlr_output->backend);
	if (renderer_drm_fd < 0 || backend_drm_fd < 0) {
		return;
	}

	dev_t render_dev, scanout_dev;
	if (!devid_from_fd(renderer_drm_fd, &render_dev) ||
			!devid_from_fd(backend_drm_fd, &scanout_dev)) {
		return;
	}

	const struct wlr_drm_format_set *output_formats =
		wlr_output_get_primary_formats(output->wlr_output,
		WLR_BUFFER_CAP_DMABUF);
	if (!output_formats) {
		return;
	}

	struct wlr_drm_format_set scanout_formats = {0};
	if (!wlr_drm_format_set_intersect(&scanout_formats,
			output_formats, renderer_formats)) {
		return;
	}

	struct wlr_linux_dmabuf_feedback_v1_tranche tranches[] = {
		{
			.target_device = scanout_dev,
			.flags = ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT,
			.formats = &scanout_formats,
		},
		{
			.target_device = render_dev,
			.formats = renderer_formats,
		},
	};

	const struct wlr_linux_dmabuf_feedback_v1 feedback = {
		.main_device = render_dev,
		.tranches = tranches,
		.tranches_len = sizeof(tranches) / sizeof(tranches[0]),
	};
	wlr_linux_dmabuf_v1_set_surface_feedback(server.linux_dmabuf_v1,
		container->view->surface, &feedback);

	wlr_drm_format_set_finish(&scanout_formats);
}

static void container_fullscreen_workspace(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return;
	}
	if (!wmiiv_assert(window->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	set_fullscreen(window, true);
	window->pending.fullscreen_mode = FULLSCREEN_WORKSPACE;

	window->saved_x = window->pending.x;
	window->saved_y = window->pending.y;
	window->saved_width = window->pending.width;
	window->saved_height = window->pending.height;

	if (window->pending.workspace) {
		window->pending.workspace->fullscreen = window;
		struct wmiiv_seat *seat;
		struct wmiiv_workspace *focus_workspace;
		wl_list_for_each(seat, &server.input->seats, link) {
			focus_workspace = seat_get_focused_workspace(seat);
			if (focus_workspace == window->pending.workspace) {
				seat_set_focus_window(seat, window);
			} else {
				struct wmiiv_node *focus =
					seat_get_focus_inactive(seat, &root->node);
				seat_set_raw_focus(seat, &window->node);
				seat_set_raw_focus(seat, focus);
			}
		}
	}

	container_end_mouse_operation(window);
	ipc_event_window(window, "fullscreen_mode");
}

static void container_fullscreen_global(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return;
	}
	if (!wmiiv_assert(window->pending.fullscreen_mode == FULLSCREEN_NONE,
				"Expected a non-fullscreen container")) {
		return;
	}
	set_fullscreen(window, true);

	root->fullscreen_global = window;
	window->saved_x = window->pending.x;
	window->saved_y = window->pending.y;
	window->saved_width = window->pending.width;
	window->saved_height = window->pending.height;

	struct wmiiv_seat *seat;
	wl_list_for_each(seat, &server.input->seats, link) {
		struct wmiiv_container *focus = seat_get_focused_container(seat);
		if (focus && focus != window) {
			seat_set_focus_window(seat, window);
		}
	}

	window->pending.fullscreen_mode = FULLSCREEN_GLOBAL;
	container_end_mouse_operation(window);
	ipc_event_window(window, "fullscreen_mode");
}

void container_fullscreen_disable(struct wmiiv_container *window) {
	if (!wmiiv_assert(container_is_window(window), "Expected window")) {
		return;
	}
	if (!wmiiv_assert(window->pending.fullscreen_mode != FULLSCREEN_NONE,
				"Expected a fullscreen container")) {
		return;
	}
	set_fullscreen(window, false);

	if (window_is_floating(window)) {
		window->pending.x = window->saved_x;
		window->pending.y = window->saved_y;
		window->pending.width = window->saved_width;
		window->pending.height = window->saved_height;
	}

	if (window->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
		if (window->pending.workspace) {
			window->pending.workspace->fullscreen = NULL;
			if (window_is_floating(window)) {
				struct wmiiv_output *output =
					window_floating_find_output(window);
				if (window->pending.workspace->output != output) {
					window_floating_move_to_center(window);
				}
			}
		}
	} else {
		root->fullscreen_global = NULL;
	}

	// If the container was mapped as fullscreen and set as floating by
	// criteria, it needs to be reinitialized as floating to get the proper
	// size and location
	if (window_is_floating(window) && (window->pending.width == 0 || window->pending.height == 0)) {
		window_floating_resize_and_center(window);
	}

	window->pending.fullscreen_mode = FULLSCREEN_NONE;
	container_end_mouse_operation(window);
	ipc_event_window(window, "fullscreen_mode");
}

void container_set_fullscreen(struct wmiiv_container *container,
		enum wmiiv_fullscreen_mode mode) {
	if (container->pending.fullscreen_mode == mode) {
		return;
	}

	switch (mode) {
	case FULLSCREEN_NONE:
		container_fullscreen_disable(container);
		break;
	case FULLSCREEN_WORKSPACE:
		// TODO (wmiiv) if disabling previous fullscreen window is
		// neccessary, why are these disable/enable functions public
		// and non-static.
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (container->pending.workspace && container->pending.workspace->fullscreen) {
			container_fullscreen_disable(container->pending.workspace->fullscreen);
		}
		container_fullscreen_workspace(container);
		break;
	case FULLSCREEN_GLOBAL:
		if (root->fullscreen_global) {
			container_fullscreen_disable(root->fullscreen_global);
		}
		if (container->pending.fullscreen_mode == FULLSCREEN_WORKSPACE) {
			container_fullscreen_disable(container);
		}
		container_fullscreen_global(container);
		break;
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

list_t *container_get_siblings(struct wmiiv_container *container) {
	if (container->pending.parent) {
		return container->pending.parent->pending.children;
	}
	if (list_find(container->pending.workspace->tiling, container) != -1) {
		return container->pending.workspace->tiling;
	}
	return container->pending.workspace->floating;
}

int container_sibling_index(struct wmiiv_container *child) {
	return list_find(container_get_siblings(child), child);
}

list_t *container_get_current_siblings(struct wmiiv_container *container) {
	if (container->current.parent) {
		return container->current.parent->current.children;
	}
	return container->current.workspace->current.tiling;
}

void container_handle_fullscreen_reparent(struct wmiiv_container *container) {
	if (container->pending.fullscreen_mode != FULLSCREEN_WORKSPACE || !container->pending.workspace ||
			container->pending.workspace->fullscreen == container) {
		return;
	}
	if (container->pending.workspace->fullscreen) {
		container_fullscreen_disable(container->pending.workspace->fullscreen);
	}
	container->pending.workspace->fullscreen = container;

	arrange_workspace(container->pending.workspace);
}

bool container_is_transient_for(struct wmiiv_container *child,
		struct wmiiv_container *ancestor) {
	return config->popup_during_fullscreen == POPUP_SMART &&
		child->view && ancestor->view &&
		view_is_transient_for(child->view, ancestor->view);
}

void container_raise_floating(struct wmiiv_container *window) {
	// Bring container to front by putting it at the end of the floating list.
	if (window_is_floating(window) && window->pending.workspace) {
		list_move_to_end(window->pending.workspace->floating, window);
		node_set_dirty(&window->pending.workspace->node);
	}
}

bool container_is_sticky(struct wmiiv_container *container) {
	return container_is_window(container) && container->is_sticky && window_is_floating(container);
}

bool container_is_sticky_or_child(struct wmiiv_container *container) {
	return container_is_sticky(container_toplevel_ancestor(container));
}
