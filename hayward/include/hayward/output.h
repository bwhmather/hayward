#ifndef HAYWARD_OUTPUT_H
#define HAYWARD_OUTPUT_H

#include <pixman.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <wayland-server-protocol.h>

#include <hayward/config.h>

#include <config.h>

struct hayward_server;
struct hayward_window;
struct hayward_view;

struct hayward_output_state {
    int x, y;
    int width, height;

    // Cached reference to the first fullscreen window on the active
    // workspace for this output.  Null if no active workspace or no
    // fullscreen window on this output.
    struct hayward_window *fullscreen_window;

    bool dead;
};

struct hayward_output {
    size_t id;

    bool dirty;

    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    struct hayward_server *server;
    struct wl_list link;

    struct wl_list shell_layers[4]; // hayward_layer_surface::link
    struct wlr_box usable_area;

    struct timespec last_frame;

    int lx, ly;        // layout coords
    int width, height; // transformed buffer size
    enum wl_output_subpixel detected_subpixel;
    enum scale_filter_mode scale_filter;
    // last applied mode when the output is DPMS'ed
    struct wlr_output_mode *current_mode;

    bool enabling, enabled;

    struct hayward_output_state pending;
    struct hayward_output_state committed;
    struct hayward_output_state current;

    struct wlr_scene_tree *scene_tree;

    struct {
        struct wlr_scene_tree *shell_background;
        struct wlr_scene_tree *shell_bottom;
        struct wlr_scene_tree *tiling;
        struct wlr_scene_tree *fullscreen;
        struct wlr_scene_tree *shell_top;
        struct wlr_scene_tree *shell_overlay;
    } layers;

    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener mode;
    struct wl_listener present;
    struct wl_listener frame;
    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal disable;
        struct wl_signal begin_destroy;
    } events;

    struct timespec last_presentation;
    uint32_t refresh_nsec;
    int max_render_time; // In milliseconds
    struct wl_event_source *repaint_timer;
};

struct hayward_output *
output_create(struct wlr_output *wlr_output);

bool
output_is_alive(struct hayward_output *output);

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

void
output_get_box(struct hayward_output *output, struct wlr_box *box);
void
output_get_usable_area(struct hayward_output *output, struct wlr_box *box);

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
