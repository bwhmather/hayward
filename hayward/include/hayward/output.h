#ifndef HWD_OUTPUT_H
#define HWD_OUTPUT_H

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

struct hwd_server;
struct hwd_window;
struct hwd_view;

struct hwd_output_state {
    int x, y;
    int width, height;

    // Cached reference to the first fullscreen window on the active
    // workspace for this output.  Null if no active workspace or no
    // fullscreen window on this output.
    struct hwd_window *fullscreen_window;

    bool dead;
};

struct hwd_output {
    size_t id;

    bool dirty;

    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    struct hwd_server *server;
    struct wl_list link;

    struct wl_list shell_layers[4]; // hwd_layer_surface::link
    struct wlr_box usable_area;

    struct timespec last_frame;

    int lx, ly;        // layout coords
    int width, height; // transformed buffer size
    enum wl_output_subpixel detected_subpixel;
    enum scale_filter_mode scale_filter;
    // last applied mode when the output is DPMS'ed
    struct wlr_output_mode *current_mode;

    bool enabling, enabled;

    struct hwd_output_state pending;
    struct hwd_output_state committed;
    struct hwd_output_state current;

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

struct hwd_output *
output_create(struct wlr_output *wlr_output);

bool
output_is_alive(struct hwd_output *output);

void
output_enable(struct hwd_output *output);

void
output_disable(struct hwd_output *output);

struct hwd_output *
output_from_wlr_output(struct wlr_output *output);

void
output_reconcile(struct hwd_output *output);

struct hwd_output *
output_get_in_direction(struct hwd_output *reference, enum wlr_direction direction);

void
output_get_box(struct hwd_output *output, struct wlr_box *box);
void
output_get_usable_area(struct hwd_output *output, struct wlr_box *box);

typedef void (*hwd_surface_iterator_func_t
)(struct hwd_output *output, struct hwd_view *view, struct wlr_surface *surface,
  struct wlr_box *box, void *user_data);

// this ONLY includes the enabled outputs
struct hwd_output *
output_by_name_or_id(const char *name_or_id);

// this includes all the outputs, including disabled ones
struct hwd_output *
all_output_by_name_or_id(const char *name_or_id);

void
handle_output_layout_change(struct wl_listener *listener, void *data);

void
handle_output_manager_apply(struct wl_listener *listener, void *data);

void
handle_output_manager_test(struct wl_listener *listener, void *data);

void
handle_output_power_manager_set_mode(struct wl_listener *listener, void *data);

#endif
