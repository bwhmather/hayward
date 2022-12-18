#ifndef _HAYWARD_OUTPUT_H
#define _HAYWARD_OUTPUT_H
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

#include "hayward/tree/node.h"
#include "hayward/tree/view.h"

#include "config.h"

struct hayward_server;
struct hayward_window;

struct hayward_output_state {
    int x, y;
    int width, height;

    // Cached reference to the first fullscreen window on the active
    // workspace for this output.  Null if no active workspace or no
    // fullscreen window on this output.
    struct hayward_window *fullscreen_window;
};

struct hayward_output {
    struct hayward_node node;
    struct wlr_output *wlr_output;
    struct hayward_server *server;
    struct wl_list link;

    struct wl_list layers[4]; // hayward_layer_surface::link
    struct wlr_box usable_area;

    struct timespec last_frame;
    struct wlr_output_damage *damage;

    int lx, ly;        // layout coords
    int width, height; // transformed buffer size
    enum wl_output_subpixel detected_subpixel;
    enum scale_filter_mode scale_filter;
    // last applied mode when the output is DPMS'ed
    struct wlr_output_mode *current_mode;

    bool enabling, enabled;

    struct hayward_output_state current;
    struct hayward_output_state pending;

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

struct hayward_output *
output_create(struct wlr_output *wlr_output);

void
output_destroy(struct hayward_output *output);

void
output_begin_destroy(struct hayward_output *output);

struct hayward_output *
output_from_wlr_output(struct wlr_output *output);

struct hayward_output *
output_get_in_direction(
    struct hayward_output *reference, enum wlr_direction direction
);

void
output_reconcile(struct hayward_output *output);

typedef void (*hayward_surface_iterator_func_t
)(struct hayward_output *output, struct hayward_view *view,
  struct wlr_surface *surface, struct wlr_box *box, void *user_data);

void
output_damage_whole(struct hayward_output *output);

void
output_damage_surface(
    struct hayward_output *output, double ox, double oy,
    struct wlr_surface *surface, bool whole
);

void
output_damage_from_view(
    struct hayward_output *output, struct hayward_view *view
);

void
output_damage_box(struct hayward_output *output, struct wlr_box *box);

void
output_damage_window(
    struct hayward_output *output, struct hayward_window *window
);
void
output_damage_column(
    struct hayward_output *output, struct hayward_column *column
);

// this ONLY includes the enabled outputs
struct hayward_output *
output_by_name_or_id(const char *name_or_id);

// this includes all the outputs, including disabled ones
struct hayward_output *
all_output_by_name_or_id(const char *name_or_id);

void
output_enable(struct hayward_output *output);

void
output_disable(struct hayward_output *output);

bool
output_has_opaque_overlay_layer_surface(struct hayward_output *output);

void
output_render(
    struct hayward_output *output, struct timespec *when,
    pixman_region32_t *damage
);

void
output_surface_for_each_surface(
    struct hayward_output *output, struct wlr_surface *surface, double ox,
    double oy, hayward_surface_iterator_func_t iterator, void *user_data
);

void
output_view_for_each_surface(
    struct hayward_output *output, struct hayward_view *view,
    hayward_surface_iterator_func_t iterator, void *user_data
);

void
output_view_for_each_popup_surface(
    struct hayward_output *output, struct hayward_view *view,
    hayward_surface_iterator_func_t iterator, void *user_data
);

void
output_layer_for_each_surface(
    struct hayward_output *output, struct wl_list *layer_surfaces,
    hayward_surface_iterator_func_t iterator, void *user_data
);

void
output_layer_for_each_toplevel_surface(
    struct hayward_output *output, struct wl_list *layer_surfaces,
    hayward_surface_iterator_func_t iterator, void *user_data
);

void
output_layer_for_each_popup_surface(
    struct hayward_output *output, struct wl_list *layer_surfaces,
    hayward_surface_iterator_func_t iterator, void *user_data
);

#if HAVE_XWAYLAND
void
output_unmanaged_for_each_surface(
    struct hayward_output *output, struct wl_list *unmanaged,
    hayward_surface_iterator_func_t iterator, void *user_data
);
#endif

void
output_drag_icons_for_each_surface(
    struct hayward_output *output, struct wl_list *drag_icons,
    hayward_surface_iterator_func_t iterator, void *user_data
);

void
output_get_box(struct hayward_output *output, struct wlr_box *box);
void
output_get_usable_area(struct hayward_output *output, struct wlr_box *box);

void
render_rect(
    struct hayward_output *output, pixman_region32_t *output_damage,
    const struct wlr_box *_box, float color[static 4]
);

void
premultiply_alpha(float color[4], float opacity);

void
scale_box(struct wlr_box *box, float scale);

enum wlr_direction
opposite_direction(enum wlr_direction d);

void
handle_output_layout_change(struct wl_listener *listener, void *data);

void
handle_output_manager_apply(struct wl_listener *listener, void *data);

void
handle_output_manager_test(struct wl_listener *listener, void *data);

void
handle_output_power_manager_set_mode(struct wl_listener *listener, void *data);

#endif
