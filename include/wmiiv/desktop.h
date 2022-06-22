#include <wlr/types/wlr_compositor.h>

struct wmiiv_column;
struct wmiiv_container;
struct wmiiv_view;

void desktop_damage_surface(struct wlr_surface *surface, double lx, double ly,
	bool whole);

void desktop_damage_column(struct wmiiv_column *column);

void desktop_damage_window(struct wmiiv_container *window);

void desktop_damage_box(struct wlr_box *box);

void desktop_damage_view(struct wmiiv_view *view);
