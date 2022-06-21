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

	if (container_is_window(container)) {
		window_end_mouse_operation(container);
	}

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

