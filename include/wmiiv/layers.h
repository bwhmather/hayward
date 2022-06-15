#ifndef _WMIIV_LAYERS_H
#define _WMIIV_LAYERS_H
#include <stdbool.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_layer_shell_v1.h>

enum layer_parent {
	LAYER_PARENT_LAYER,
	LAYER_PARENT_POPUP,
};

struct wmiiv_layer_surface {
	struct wlr_layer_surface_v1 *layer_surface;
	struct wl_list link;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
	struct wl_listener output_destroy;
	struct wl_listener new_popup;
	struct wl_listener new_subsurface;

	struct wlr_box geo;
	bool mapped;
	struct wlr_box extent;
	enum zwlr_layer_shell_v1_layer layer;

	struct wl_list subsurfaces;
};

struct wmiiv_layer_popup {
	struct wlr_xdg_popup *wlr_popup;
	enum layer_parent parent_type;
	union {
		struct wmiiv_layer_surface *parent_layer;
		struct wmiiv_layer_popup *parent_popup;
	};
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener new_popup;
};

struct wmiiv_layer_subsurface {
	struct wlr_subsurface *wlr_subsurface;
	struct wmiiv_layer_surface *layer_surface;
	struct wl_list link;

	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener commit;
};

struct wmiiv_output;
void arrange_layers(struct wmiiv_output *output);

struct wmiiv_layer_surface *layer_from_wlr_layer_surface_v1(
	struct wlr_layer_surface_v1 *layer_surface);

#endif