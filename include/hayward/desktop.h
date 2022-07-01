#include <wlr/types/wlr_compositor.h>

struct hayward_column;
struct hayward_window;
struct hayward_view;

void desktop_damage_surface(struct wlr_surface *surface, double lx, double ly,
	bool whole);

void desktop_damage_column(struct hayward_column *column);

void desktop_damage_window(struct hayward_window *window);

void desktop_damage_box(struct wlr_box *box);

void desktop_damage_view(struct hayward_view *view);
