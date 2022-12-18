#ifndef _HAYWARD_DECORATION_H
#define _HAYWARD_DECORATION_H

#include <wlr/types/wlr_server_decoration.h>

struct hayward_server_decoration {
	struct wlr_server_decoration *wlr_server_decoration;
	struct wl_list link;

	struct wl_listener destroy;
	struct wl_listener mode;
};

struct hayward_server_decoration *
decoration_from_surface(struct wlr_surface *surface);

#endif
