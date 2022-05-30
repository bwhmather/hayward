#ifndef _WMIIV_OUTPUT_H
#define _WMIIV_OUTPUT_H
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include "config.h"
#include "wmiiv/tree/node.h"
#include "wmiiv/tree/view.h"

struct wmiiv_server;
struct wmiiv_container;

struct wmiiv_output_state {
	list_t *workspaces;
	struct wmiiv_workspace *active_workspace;
};

struct wmiiv_output {
	struct wmiiv_node node;
	struct wlr_output *wlr_output;
	struct wmiiv_server *server;
	struct wl_list link;

	struct wl_list layers[4]; // wmiiv_layer_surface::link
	struct wlr_box usable_area;

	struct timespec last_frame;
	struct wlr_output_damage *damage;

	int lx, ly; // layout coords
	int width, height; // transformed buffer size
	enum wl_output_subpixel detected_subpixel;
	enum scale_filter_mode scale_filter;
	// last applied mode when the output is DPMS'ed
	struct wlr_output_mode *current_mode;

	bool enabling, enabled;
	list_t *workspaces;

	struct wmiiv_output_state current;

	struct wl_listener destroy;
	struct wl_listener commit;
	struct wl_listener mode;
	struct wl_listener present;
	struct wl_listener damage_destroy;
	struct wl_listener damage_frame;

	struct {
		struct wl_signal disable;
	} events;

	struct timespec last_presentation;
	uint32_t refresh_nsec;
	int max_render_time; // In milliseconds
	struct wl_event_source *repaint_timer;
};

struct wmiiv_output *output_create(struct wlr_output *wlr_output);

void output_destroy(struct wmiiv_output *output);

void output_begin_destroy(struct wmiiv_output *output);

struct wmiiv_output *output_from_wlr_output(struct wlr_output *output);

struct wmiiv_output *output_get_in_direction(struct wmiiv_output *reference,
		enum wlr_direction direction);

void output_add_workspace(struct wmiiv_output *output,
		struct wmiiv_workspace *workspace);

typedef void (*wmiiv_surface_iterator_func_t)(struct wmiiv_output *output,
	struct wmiiv_view *view, struct wlr_surface *surface, struct wlr_box *box,
	void *user_data);

void output_damage_whole(struct wmiiv_output *output);

void output_damage_surface(struct wmiiv_output *output, double ox, double oy,
	struct wlr_surface *surface, bool whole);

void output_damage_from_view(struct wmiiv_output *output,
	struct wmiiv_view *view);

void output_damage_box(struct wmiiv_output *output, struct wlr_box *box);

void output_damage_whole_container(struct wmiiv_output *output,
	struct wmiiv_container *container);

// this ONLY includes the enabled outputs
struct wmiiv_output *output_by_name_or_id(const char *name_or_id);

// this includes all the outputs, including disabled ones
struct wmiiv_output *all_output_by_name_or_id(const char *name_or_id);

void output_sort_workspaces(struct wmiiv_output *output);

void output_enable(struct wmiiv_output *output);

void output_disable(struct wmiiv_output *output);

bool output_has_opaque_overlay_layer_surface(struct wmiiv_output *output);

struct wmiiv_workspace *output_get_active_workspace(struct wmiiv_output *output);

void output_render(struct wmiiv_output *output, struct timespec *when,
	pixman_region32_t *damage);

void output_surface_for_each_surface(struct wmiiv_output *output,
		struct wlr_surface *surface, double ox, double oy,
		wmiiv_surface_iterator_func_t iterator, void *user_data);

void output_view_for_each_surface(struct wmiiv_output *output,
	struct wmiiv_view *view, wmiiv_surface_iterator_func_t iterator,
	void *user_data);

void output_view_for_each_popup_surface(struct wmiiv_output *output,
		struct wmiiv_view *view, wmiiv_surface_iterator_func_t iterator,
		void *user_data);

void output_layer_for_each_surface(struct wmiiv_output *output,
	struct wl_list *layer_surfaces, wmiiv_surface_iterator_func_t iterator,
	void *user_data);

void output_layer_for_each_toplevel_surface(struct wmiiv_output *output,
	struct wl_list *layer_surfaces, wmiiv_surface_iterator_func_t iterator,
	void *user_data);

void output_layer_for_each_popup_surface(struct wmiiv_output *output,
	struct wl_list *layer_surfaces, wmiiv_surface_iterator_func_t iterator,
	void *user_data);

#if HAVE_XWAYLAND
void output_unmanaged_for_each_surface(struct wmiiv_output *output,
	struct wl_list *unmanaged, wmiiv_surface_iterator_func_t iterator,
	void *user_data);
#endif

void output_drag_icons_for_each_surface(struct wmiiv_output *output,
	struct wl_list *drag_icons, wmiiv_surface_iterator_func_t iterator,
	void *user_data);

void output_for_each_workspace(struct wmiiv_output *output,
		void (*f)(struct wmiiv_workspace *ws, void *data), void *data);

void output_for_each_container(struct wmiiv_output *output,
		void (*f)(struct wmiiv_container *container, void *data), void *data);

struct wmiiv_workspace *output_find_workspace(struct wmiiv_output *output,
		bool (*test)(struct wmiiv_workspace *ws, void *data), void *data);

struct wmiiv_container *output_find_container(struct wmiiv_output *output,
		bool (*test)(struct wmiiv_container *container, void *data), void *data);

void output_get_box(struct wmiiv_output *output, struct wlr_box *box);

enum wmiiv_container_layout output_get_default_layout(
		struct wmiiv_output *output);

void render_rect(struct wmiiv_output *output,
		pixman_region32_t *output_damage, const struct wlr_box *_box,
		float color[static 4]);

void premultiply_alpha(float color[4], float opacity);

void scale_box(struct wlr_box *box, float scale);

enum wlr_direction opposite_direction(enum wlr_direction d);

void handle_output_layout_change(struct wl_listener *listener, void *data);

void handle_output_manager_apply(struct wl_listener *listener, void *data);

void handle_output_manager_test(struct wl_listener *listener, void *data);

void handle_output_power_manager_set_mode(struct wl_listener *listener,
	void *data);

#endif
