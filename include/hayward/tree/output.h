#ifndef HWD_TREE_OUTPUT_H
#define HWD_TREE_OUTPUT_H

#include <config.h>

#include <stdbool.h>
#include <stddef.h>

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

struct hwd_server;
struct hwd_window;
struct hwd_view;

enum scale_filter_mode {
    SCALE_FILTER_DEFAULT, // the default is currently smart
    SCALE_FILTER_LINEAR,
    SCALE_FILTER_NEAREST,
    SCALE_FILTER_SMART,
};

struct hwd_output_state {
    int x, y;
    int width, height;

    bool dead;
};

struct hwd_output {
    size_t id;

    bool dirty;

    struct wlr_output *wlr_output;

    struct wl_list link;

    struct wl_list shell_layers[4]; // hwd_layer_surface::link
    struct wlr_box usable_area;

    enum wl_output_subpixel detected_subpixel;
    enum scale_filter_mode scale_filter;
    // last applied mode when the output is DPMS'ed
    struct wlr_output_mode *current_mode;

    bool enabling, enabled;

    struct hwd_output_state pending;
    struct hwd_output_state committed;
    struct hwd_output_state current;

    struct wlr_scene_tree *scene_tree_background;
    struct wlr_scene_tree *scene_tree_overlay;

    struct {
        struct wlr_scene_tree *shell_background;
        struct wlr_scene_tree *shell_bottom;
        struct wlr_scene_tree *fullscreen;
        struct wlr_scene_tree *shell_top;
        struct wlr_scene_tree *shell_overlay;
    } layers;

    struct wl_listener destroy;
    struct wl_listener request_state;
    struct wl_listener transaction_commit;
    struct wl_listener transaction_apply;
    struct wl_listener transaction_after_apply;

    struct {
        struct wl_signal disable;
        struct wl_signal begin_destroy;
    } events;
};

struct hwd_output *
output_create(struct wlr_output *wlr_output);

struct hwd_output *
output_from_wlr_output(struct wlr_output *output);

void
output_reconcile(struct hwd_output *output);

void
output_arrange(struct hwd_output *wlr_output);

void
output_get_box(struct hwd_output *output, struct wlr_box *box);
void
output_get_usable_area(struct hwd_output *output, struct wlr_box *box);

typedef void (*hwd_surface_iterator_func_t)(
    struct hwd_output *output, struct hwd_view *view, struct wlr_surface *surface,
    struct wlr_box *box, void *user_data
);

// this ONLY includes the enabled outputs
struct hwd_output *
output_by_name_or_id(const char *name_or_id);

void
handle_output_layout_change(struct wl_listener *listener, void *data);

#endif
