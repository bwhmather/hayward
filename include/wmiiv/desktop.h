#include <wlr/types/wlr_compositor.h>

struct wmiiv_container;
struct wmiiv_view;

void desktop_damage_surface(struct wlr_surface *surface, double lx, double ly,
	bool whole);

void desktop_damage_whole_container(struct wmiiv_container *con);

void desktop_damage_box(struct wlr_box *box);

void desktop_damage_view(struct wmiiv_view *view);
