#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/window.h"

#include <cairo.h>
#include <drm_fourcc.h>
#include <float.h>
#include <glib-object.h>
#include <limits.h>
#include <math.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

#include <hayward-common/cairo_util.h>
#include <hayward-common/list.h>
#include <hayward-common/log.h>
#include <hayward-common/pango.h>

#include <wayland-server-protocol.h>

#include <hayward/config.h>
#include <hayward/desktop/transaction.h>
#include <hayward/globals/root.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/ipc-server.h>
#include <hayward/output.h>
#include <hayward/scene/text.h>
#include <hayward/server.h>
#include <hayward/tree/arrange.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/workspace.h>

#include <config.h>

static const struct wlr_addon_interface scene_tree_marker_interface = {
    .name = "hayward_window", .destroy = NULL};

static void
window_destroy(struct hayward_window *window);

static bool
window_should_configure(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    if (window->committed.dead) {
        return false;
    }
    // TODO if the window's view initiated the change, it should not be
    // reconfigured.
    struct hayward_window_state *cstate = &window->current;
    struct hayward_window_state *nstate = &window->committed;

#if HAVE_XWAYLAND
    // Xwayland views are position-aware and need to be reconfigured
    // when their position changes.
    if (window->view->type == HAYWARD_VIEW_XWAYLAND) {
        // Hayward logical coordinates are doubles, but they get truncated to
        // integers when sent to Xwayland through `xcb_configure_window`.
        // X11 apps will not respond to duplicate configure requests (from their
        // truncated point of view) and cause transactions to time out.
        if ((int)cstate->content_x != (int)nstate->content_x ||
            (int)cstate->content_y != (int)nstate->content_y) {
            return true;
        }
    }
#endif
    if (cstate->content_width == nstate->content_width &&
        cstate->content_height == nstate->content_height) {
        return false;
    }
    return true;
}

static void
window_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hayward_window *window =
        wl_container_of(listener, window, transaction_commit);

    wl_list_remove(&listener->link);
    transaction_add_apply_listener(&window->transaction_apply);

    memcpy(
        &window->committed, &window->pending,
        sizeof(struct hayward_window_state)
    );
    window->dirty = false;

    bool hidden = !window->committed.dead && !view_is_visible(window->view);
    if (window_should_configure(window)) {
        struct hayward_window_state *state = &window->committed;

        window->configure_serial = view_configure(
            window->view, state->content_x, state->content_y,
            state->content_width, state->content_height
        );
        if (!hidden) {
            transaction_acquire();
            window->is_configuring = true;
        }

        // From here on we are rendering a saved buffer of the view, which
        // means we can send a frame done event to make the client redraw it
        // as soon as possible. Additionally, this is required if a view is
        // mapping and its default geometry doesn't intersect an output.
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_surface_send_frame_done(window->view->surface, &now);
    }
    if (!hidden && window->view->saved_surface_tree == NULL) {
        view_save_buffer(window->view);
        memcpy(
            &window->view->saved_geometry, &window->view->geometry,
            sizeof(struct wlr_box)
        );
    }
}

static void
window_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hayward_window *window =
        wl_container_of(listener, window, transaction_apply);

    wl_list_remove(&listener->link);
    window->is_configuring = false;

    double x = window->committed.x;
    double y = window->committed.y;
    double width = window->committed.width;
    double height = window->committed.height;

    int border = window->committed.border_thickness;
    int titlebar_height = window_titlebar_height();

    struct wlr_scene_tree *parent = root->orphans;
    if (window->committed.parent) {
        parent = window->committed.parent->scene_tree;
    } else {
        parent = window->committed.workspace->layers.floating;
    }
    wlr_scene_node_reparent(&window->scene_tree->node, parent);

    wlr_scene_node_set_position(&window->scene_tree->node, x, y);

    wlr_scene_node_set_position(
        window->layers.title_text->node, config->titlebar_h_padding,
        config->titlebar_v_padding
    );
    hayward_text_node_set_text(
        window->layers.title_text, window->formatted_title
    );
    hayward_text_node_set_max_width(
        window->layers.title_text,
        width - 2 * border - 2 * config->titlebar_h_padding
    );

    wlr_scene_node_set_position(
        &window->layers.title_border->node, border, titlebar_height + border
    );
    wlr_scene_rect_set_size(
        window->layers.title_border, width - 2 * border, border
    );

    wlr_scene_node_set_position(&window->layers.border_top->node, border, 0);
    wlr_scene_rect_set_size(
        window->layers.border_top, width - 2 * border, border
    );

    wlr_scene_node_set_position(
        &window->layers.border_bottom->node, border, height - border
    );
    wlr_scene_rect_set_size(
        window->layers.border_top, width - 2 * border, border
    );

    wlr_scene_node_set_position(&window->layers.border_left->node, 0, 0);
    wlr_scene_rect_set_size(window->layers.border_left, border, height);
    wlr_scene_node_set_position(
        &window->layers.border_left->node, width - border, 0
    );
    wlr_scene_rect_set_size(window->layers.border_right, border, height);

    wlr_scene_node_set_enabled(
        &window->layers.content_tree->node, !window->committed.shaded
    );
    wlr_scene_node_set_position(
        &window->layers.content_tree->node, window->committed.border_thickness,
        titlebar_height + 2 * window->committed.border_thickness
    );

    struct hayward_view *view = window->view;

    if (view->saved_surface_tree != NULL) {
        view_remove_saved_buffer(view);
    }

    // If the view hasn't responded to the configure, center it within
    // the window. This is important for fullscreen views which
    // refuse to resize to the size of the output.
    if (view->surface) {
        view_center_surface(view);
    }

	wlr_scene_node_reparent(&view->scene_tree->node, window->layers.content_tree);

    if (window->current.dead) {
        transaction_add_after_apply_listener(&window->transaction_after_apply);
    }

    memcpy(
        &window->current, &window->committed,
        sizeof(struct hayward_window_state)
    );
}

static void
window_handle_transaction_after_apply(
    struct wl_listener *listener, void *data
) {
    struct hayward_window *window =
        wl_container_of(listener, window, transaction_after_apply);

    wl_list_remove(&listener->link);

    hayward_assert(window->current.dead, "After apply called on live window");
    window_destroy(window);
}

struct hayward_window *
window_create(struct hayward_view *view) {
    struct hayward_window *window = calloc(1, sizeof(struct hayward_window));
    if (!window) {
        hayward_log(HAYWARD_ERROR, "Unable to allocate hayward_window");
        return NULL;
    }

    static size_t next_id = 1;
    window->id = next_id++;

    wl_signal_init(&window->events.begin_destroy);
    wl_signal_init(&window->events.destroy);

    window->view = view;
    window->alpha = 1.0f;

    window->transaction_commit.notify = window_handle_transaction_commit;
    window->transaction_apply.notify = window_handle_transaction_apply;
    window->transaction_after_apply.notify =
        window_handle_transaction_after_apply;

    window->scene_tree = wlr_scene_tree_create(root->orphans); // TODO
    hayward_assert(window->scene_tree != NULL, "Allocation failed");
    wlr_addon_init(
        &window->scene_tree_marker, &window->scene_tree->node.addons,
        &scene_tree_marker_interface, &scene_tree_marker_interface
    );

    const float border_color[] = {1.0, 0.0, 0.0, 1.0};
    const float text_color[] = {1.0, 1.0, 1.0, 1.0};

    window->layers.title_tree = wlr_scene_tree_create(window->scene_tree);
    hayward_assert(window->layers.title_tree != NULL, "Allocation failed");
    window->layers.title_background = wlr_scene_rect_create(
        window->layers.title_tree, 0, 0, (const float *)border_color
    );
    hayward_assert(
        window->layers.title_background != NULL, "Allocation failed"
    );
    window->layers.title_text = hayward_text_node_create(
        window->layers.title_tree, "", text_color, config->pango_markup
    );
    hayward_assert(
        window->layers.title_background != NULL, "Allocation failed"
    );
    window->layers.title_border = wlr_scene_rect_create(
        window->layers.title_tree, 0, 0, (const float *)border_color
    );
    hayward_assert(window->layers.title_border != NULL, "Allocation failed");

    window->layers.border_tree = wlr_scene_tree_create(window->scene_tree);
    hayward_assert(window->layers.border_tree != NULL, "Allocation failed");
    window->layers.border_top = wlr_scene_rect_create(
        window->layers.border_tree, 0, 0, (const float *)border_color
    );
    hayward_assert(window->layers.border_top != NULL, "Allocation failed");
    window->layers.border_bottom = wlr_scene_rect_create(
        window->layers.border_tree, 0, 0, (const float *)border_color
    );
    hayward_assert(window->layers.border_bottom != NULL, "Allocation failed");
    window->layers.border_left = wlr_scene_rect_create(
        window->layers.border_tree, 0, 0, (const float *)border_color
    );
    hayward_assert(window->layers.border_left != NULL, "Allocation failed");
    window->layers.border_right = wlr_scene_rect_create(
        window->layers.border_tree, 0, 0, (const float *)border_color
    );
    hayward_assert(window->layers.border_right != NULL, "Allocation failed");

    window->layers.content_tree = wlr_scene_tree_create(window->scene_tree);
    hayward_assert(window->layers.content_tree != NULL, "Allocation failed");

    window_set_dirty(window);

    return window;
}

bool
window_is_alive(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    return !window->pending.dead;
}

static void
window_destroy(struct hayward_window *window) {
    hayward_assert(
        window->current.dead,
        "Tried to free window which wasn't marked as destroying"
    );
    hayward_assert(
        !window->dirty,
        "Tried to free window which is queued for the next transaction"
    );

    wlr_scene_node_destroy(&window->layers.content_tree->node);

    wlr_scene_node_destroy(&window->layers.border_right->node);
    wlr_scene_node_destroy(&window->layers.border_left->node);
    wlr_scene_node_destroy(&window->layers.border_bottom->node);
    wlr_scene_node_destroy(&window->layers.border_top->node);
    wlr_scene_node_destroy(&window->layers.border_tree->node);

    wlr_scene_node_destroy(&window->layers.title_border->node);
    wlr_scene_node_destroy(window->layers.title_text->node);
    wlr_scene_node_destroy(&window->layers.title_background->node);
    wlr_scene_node_destroy(&window->layers.title_tree->node);

    wlr_scene_node_destroy(&window->scene_tree->node);

    free(window->title);
    free(window->formatted_title);
    wlr_texture_destroy(window->title_focused);
    wlr_texture_destroy(window->title_focused_inactive);
    wlr_texture_destroy(window->title_unfocused);
    wlr_texture_destroy(window->title_urgent);
    wlr_texture_destroy(window->title_focused_tab_title);

    if (window->view->window == window) {
        window->view->window = NULL;
        if (window->view->destroying) {
            view_destroy(window->view);
        }
    }

    free(window);
}

void
window_begin_destroy(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_alive(window), "Expected live window");

    window->pending.dead = true;

    ipc_event_window(window, "close");

    window_end_mouse_operation(window);

    if (window->pending.parent || window->pending.workspace) {
        window_detach(window);
    }

    wl_signal_emit(&window->events.begin_destroy, window);

    window_set_dirty(window);
}

void
window_set_dirty(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");

    if (window->dirty) {
        return;
    }

    window->dirty = true;
    transaction_add_commit_listener(&window->transaction_commit);
    transaction_ensure_queued();
}

void
window_detach(struct hayward_window *window) {
    struct hayward_column *column = window->pending.parent;
    struct hayward_workspace *workspace = window->pending.workspace;

    if (workspace == NULL) {
        return;
    }

    if (column != NULL) {
        column_remove_child(column, window);
    } else {
        workspace_remove_floating(workspace, window);
    }
}

bool
window_is_attached(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");

    if (window->pending.workspace == NULL) {
        return false;
    }

    return true;
}

void
window_reconcile_floating(
    struct hayward_window *window, struct hayward_workspace *workspace
) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_alive(window), "Expected live window");
    hayward_assert(workspace != NULL, "Expected workspace");

    window->pending.workspace = workspace;
    window->pending.parent = NULL;

    window->pending.focused = workspace_is_visible(workspace) &&
        workspace_get_active_window(workspace) == window;
}

void
window_reconcile_tiling(
    struct hayward_window *window, struct hayward_column *column
) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_alive(window), "Expected live window");
    hayward_assert(column != NULL, "Expected column");

    window->pending.workspace = column->pending.workspace;
    window->pending.output = column->pending.output;
    window->pending.parent = column;

    window->pending.focused =
        column->pending.focused && window == column->pending.active_child;
}

void
window_reconcile_detached(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");

    window->pending.workspace = NULL;
    window->pending.parent = NULL;

    window->pending.focused = false;
}

void
window_end_mouse_operation(struct hayward_window *window) {
    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seatop_unref(seat, window);
    }
}

bool
window_is_floating(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(
        window_is_attached(window), "Window not attached to workspace"
    );

    if (!window->pending.parent) {
        return true;
    }

    return false;
}

bool
window_is_current_floating(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(
        window->current.workspace != NULL, "Window not attached to workspace"
    );

    if (!window->current.parent) {
        // hayward_assert(list_find(workspace->pending.floating, window) != -1,
        // "Window missing from parent list");
        return true;
    }

    return false;
}

bool
window_is_fullscreen(struct hayward_window *window) {
    return window->pending.fullscreen;
}

bool
window_is_tiling(struct hayward_window *window) {
    return window->pending.parent != NULL;
}

struct wlr_surface *
window_surface_at(
    struct hayward_window *window, double lx, double ly, double *sx, double *sy
) {
    struct hayward_view *view = window->view;
    double view_sx = lx - window->surface_x + view->geometry.x;
    double view_sy = ly - window->surface_y + view->geometry.y;

    double _sx, _sy;
    struct wlr_surface *surface = NULL;
    switch (view->type) {
#if HAVE_XWAYLAND
    case HAYWARD_VIEW_XWAYLAND:
        surface =
            wlr_surface_surface_at(view->surface, view_sx, view_sy, &_sx, &_sy);
        break;
#endif
    case HAYWARD_VIEW_XDG_SHELL:
        surface = wlr_xdg_surface_surface_at(
            view->wlr_xdg_toplevel->base, view_sx, view_sy, &_sx, &_sy
        );
        break;
    }
    if (surface) {
        *sx = _sx;
        *sy = _sy;
        return surface;
    }
    return NULL;
}

bool
window_contents_contain_point(
    struct hayward_window *window, double lx, double ly
) {
    struct wlr_box box = {
        .x = window->pending.content_x,
        .y = window->pending.content_y,
        .width = window->pending.content_width,
        .height = window->pending.content_height,
    };

    return wlr_box_contains_point(&box, lx, ly);
}

bool
window_contains_point(struct hayward_window *window, double lx, double ly) {
    struct wlr_box box = {
        .x = window->pending.x,
        .y = window->pending.y,
        .width = window->pending.width,
        .height = window->pending.height,
    };

    return wlr_box_contains_point(&box, lx, ly);
}

size_t
window_titlebar_height(void) {
    return config->font_height + config->titlebar_v_padding * 2;
}

static void
set_fullscreen(struct hayward_window *window, bool enable) {
    if (window->view->impl->set_fullscreen) {
        window->view->impl->set_fullscreen(window->view, enable);
    }
    if (window->view->foreign_toplevel) {
        wlr_foreign_toplevel_handle_v1_set_fullscreen(
            window->view->foreign_toplevel, enable
        );
    }
}

static void
window_fullscreen_disable(struct hayward_window *window) {
    struct hayward_workspace *workspace = window->pending.workspace;
    hayward_assert(workspace != NULL, "Window must be attached to a workspace");

    struct hayward_output *output = window->pending.output;
    hayward_assert(
        window->pending.output, "Window must have an associated output"
    );

    if (!window->pending.fullscreen) {
        return;
    }

    set_fullscreen(window, false);

    if (window_is_floating(window)) {
        window->pending.x = window->saved_x;
        window->pending.y = window->saved_y;
        window->pending.width = window->saved_width;
        window->pending.height = window->saved_height;
    }

    // If the container was mapped as fullscreen and set as floating by
    // criteria, it needs to be reinitialized as floating to get the proper
    // size and location
    if (window_is_floating(window) &&
        (window->pending.width == 0 || window->pending.height == 0)) {
        window_floating_resize_and_center(window);
    }

    window->pending.fullscreen = false;

    if (workspace->pending.focused) {
        output_reconcile(output);
    }

    window_end_mouse_operation(window);
    ipc_event_window(window, "fullscreen_mode");
}

static void
window_fullscreen_enable(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_alive(window), "Expected live window");

    struct hayward_workspace *workspace = window->pending.workspace;
    hayward_assert(workspace != NULL, "Window must be attached to a workspace");

    struct hayward_output *output = window->pending.output;
    hayward_assert(
        window->pending.output, "Window must have an associated output"
    );

    if (window->pending.fullscreen) {
        return;
    }

    // Disable previous fullscreen window for output and workspace.
    struct hayward_window *previous =
        workspace_get_fullscreen_window_for_output(workspace, output);
    if (previous != NULL) {
        window_fullscreen_disable(previous);
    }

    set_fullscreen(window, true);

    window->pending.fullscreen = true;

    window->saved_x = window->pending.x;
    window->saved_y = window->pending.y;
    window->saved_width = window->pending.width;
    window->saved_height = window->pending.height;

    if (workspace->pending.focused) {
        output_reconcile(output);
    }

    window_end_mouse_operation(window);
    ipc_event_window(window, "fullscreen_mode");
}

void
window_set_fullscreen(struct hayward_window *window, bool enabled) {
    if (enabled) {
        window_fullscreen_enable(window);
    } else {
        window_fullscreen_disable(window);
    }
}

void
window_handle_fullscreen_reparent(struct hayward_window *window) {
    struct hayward_workspace *workspace = window->pending.workspace;
    struct hayward_output *output = window->pending.output;

    if (!window->pending.fullscreen) {
        return;
    }

    if (!workspace) {
        return;
    }

    if (!output) {
        return;
    }

    // Temporarily mark window as not fullscreen so that we can check the
    // previous fullscreen window for the workspace.
    window->pending.fullscreen = false;
    struct hayward_window *previous =
        workspace_get_fullscreen_window_for_output(workspace, output);
    window->pending.fullscreen = true;

    if (previous) {
        window_fullscreen_disable(previous);
    }

    if (workspace->pending.focused) {
        output_reconcile(output);
    }

    arrange_workspace(window->pending.workspace);
}

void
floating_calculate_constraints(
    int *min_width, int *max_width, int *min_height, int *max_height
) {
    if (config->floating_minimum_width == -1) { // no minimum
        *min_width = 0;
    } else if (config->floating_minimum_width == 0) { // automatic
        *min_width = 75;
    } else {
        *min_width = config->floating_minimum_width;
    }

    if (config->floating_minimum_height == -1) { // no minimum
        *min_height = 0;
    } else if (config->floating_minimum_height == 0) { // automatic
        *min_height = 50;
    } else {
        *min_height = config->floating_minimum_height;
    }

    struct wlr_box box;
    wlr_output_layout_get_box(root->output_layout, NULL, &box);

    if (config->floating_maximum_width == -1) { // no maximum
        *max_width = INT_MAX;
    } else if (config->floating_maximum_width == 0) { // automatic
        *max_width = box.width;
    } else {
        *max_width = config->floating_maximum_width;
    }

    if (config->floating_maximum_height == -1) { // no maximum
        *max_height = INT_MAX;
    } else if (config->floating_maximum_height == 0) { // automatic
        *max_height = box.height;
    } else {
        *max_height = config->floating_maximum_height;
    }
}

static void
floating_natural_resize(struct hayward_window *window) {
    int min_width, max_width, min_height, max_height;
    floating_calculate_constraints(
        &min_width, &max_width, &min_height, &max_height
    );
    if (!window->view) {
        window->pending.width =
            fmax(min_width, fmin(window->pending.width, max_width));
        window->pending.height =
            fmax(min_height, fmin(window->pending.height, max_height));
    } else {
        struct hayward_view *view = window->view;
        window->pending.content_width =
            fmax(min_width, fmin(view->natural_width, max_width));
        window->pending.content_height =
            fmax(min_height, fmin(view->natural_height, max_height));
        window_set_geometry_from_content(window);
    }
}

void
window_floating_resize_and_center(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_alive(window), "Expected live window");

    struct hayward_output *output = window->pending.output;
    hayward_assert(output != NULL, "Expected output");

    struct wlr_box ob;
    wlr_output_layout_get_box(
        root->output_layout, window->pending.output->wlr_output, &ob
    );
    if (wlr_box_empty(&ob)) {
        // On NOOP output. Will be called again when moved to an output
        window->pending.x = 0;
        window->pending.y = 0;
        window->pending.width = 0;
        window->pending.height = 0;
        return;
    }

    floating_natural_resize(window);
    if (!window->view) {
        if (window->pending.width > output->pending.width ||
            window->pending.height > output->pending.height) {
            window->pending.x = ob.x + (ob.width - window->pending.width) / 2;
            window->pending.y = ob.y + (ob.height - window->pending.height) / 2;
        } else {
            window->pending.x = output->pending.x +
                (output->pending.width - window->pending.width) / 2;
            window->pending.y = output->pending.y +
                (output->pending.height - window->pending.height) / 2;
        }
    } else {
        if (window->pending.content_width > output->pending.width ||
            window->pending.content_height > output->pending.height) {
            window->pending.content_x =
                ob.x + (ob.width - window->pending.content_width) / 2;
            window->pending.content_y =
                ob.y + (ob.height - window->pending.content_height) / 2;
        } else {
            window->pending.content_x = output->pending.x +
                (output->pending.width - window->pending.content_width) / 2;
            window->pending.content_y = output->pending.y +
                (output->pending.height - window->pending.content_height) / 2;
        }

        // If the view's border is B_NONE then these properties are ignored.
        window->pending.border_top = window->pending.border_bottom = true;
        window->pending.border_left = window->pending.border_right = true;

        window_set_geometry_from_content(window);
    }
}

void
window_floating_set_default_size(struct hayward_window *window) {
    hayward_assert(
        window->pending.workspace, "Expected a window on a workspace"
    );

    int min_width, max_width, min_height, max_height;
    floating_calculate_constraints(
        &min_width, &max_width, &min_height, &max_height
    );
    struct wlr_box *box = calloc(1, sizeof(struct wlr_box));
    workspace_get_box(window->pending.workspace, box);

    double width = fmax(min_width, fmin(box->width * 0.5, max_width));
    double height = fmax(min_height, fmin(box->height * 0.75, max_height));

    window->pending.content_width = width;
    window->pending.content_height = height;
    window_set_geometry_from_content(window);

    free(box);
}

void
window_floating_move_to(
    struct hayward_window *window, struct hayward_output *output, double lx,
    double ly
) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_floating(window), "Expected a floating window");

    window->pending.x = lx;
    window->pending.y += ly;
    window->pending.content_x += lx;
    window->pending.content_y += ly;

    window_set_dirty(window);
}

void
window_floating_move_to_center(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");
    hayward_assert(window_is_floating(window), "Expected a floating window");

    struct hayward_output *output = window->pending.output;

    double new_lx =
        output->pending.x + (output->pending.width - output->pending.width) / 2;
    double new_ly = output->pending.y +
        (output->pending.height - output->pending.height) / 2;

    window_floating_move_to(window, output, new_lx, new_ly);
}

struct hayward_output *
window_get_output(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");

    return window->pending.output;
}

struct hayward_output *
window_get_current_output(struct hayward_window *window) {
    hayward_assert(window != NULL, "Expected window");

    return window->current.output;
}

void
window_get_box(struct hayward_window *window, struct wlr_box *box) {
    box->x = window->pending.x;
    box->y = window->pending.y;
    box->width = window->pending.width;
    box->height = window->pending.height;
}

/**
 * Indicate to clients in this window that they are participating in (or
 * have just finished) an interactive resize
 */
void
window_set_resizing(struct hayward_window *window, bool resizing) {
    if (!window) {
        return;
    }

    if (window->view->impl->set_resizing) {
        window->view->impl->set_resizing(window->view, resizing);
    }
}

void
window_set_geometry_from_content(struct hayward_window *window) {
    hayward_assert(window->view, "Expected a view");
    hayward_assert(window_is_floating(window), "Expected a floating view");
    size_t border_width = 0;
    size_t top = 0;

    if (window->pending.border != B_CSD && !window->pending.fullscreen) {
        border_width = window->pending.border_thickness *
            (window->pending.border != B_NONE);
        top = window->pending.border == B_NORMAL ? window_titlebar_height()
                                                 : border_width;
    }

    window->pending.x = window->pending.content_x - border_width;
    window->pending.y = window->pending.content_y - top;
    window->pending.width = window->pending.content_width + border_width * 2;
    window->pending.height =
        top + window->pending.content_height + border_width;

    window_set_dirty(window);
}

bool
window_is_transient_for(
    struct hayward_window *child, struct hayward_window *ancestor
) {
    return config->popup_during_fullscreen == POPUP_SMART && child->view &&
        ancestor->view && view_is_transient_for(child->view, ancestor->view);
}

void
window_raise_floating(struct hayward_window *window) {
    // Bring window to front by putting it at the end of the floating list.
    if (window_is_floating(window) && window->pending.workspace) {
        list_move_to_end(window->pending.workspace->pending.floating, window);
        workspace_set_dirty(window->pending.workspace);
    }
}

list_t *
window_get_siblings(struct hayward_window *window) {
    if (window_is_tiling(window)) {
        return window->pending.parent->pending.children;
    }
    if (window_is_floating(window)) {
        return window->pending.workspace->pending.floating;
    }
    return NULL;
}

int
window_sibling_index(struct hayward_window *window) {
    return list_find(window_get_siblings(window), window);
}

list_t *
window_get_current_siblings(struct hayward_window *window) {
    if (window->current.parent) {
        return window->current.parent->current.children;
    }
    if (window->current.workspace) {
        return window->current.workspace->current.floating;
    }
    return NULL;
}

struct hayward_window *
window_get_previous_sibling(struct hayward_window *window) {
    if (!window->pending.parent) {
        return NULL;
    }

    list_t *siblings = window->pending.parent->pending.children;
    int index = list_find(siblings, window);

    if (index <= 0) {
        return NULL;
    }

    return siblings->items[index - 1];
}

struct hayward_window *
window_get_next_sibling(struct hayward_window *window) {
    if (!window->pending.parent) {
        return NULL;
    }

    list_t *siblings = window->pending.parent->pending.children;
    int index = list_find(siblings, window);

    if (index < 0) {
        return NULL;
    }

    if (index >= siblings->length - 1) {
        return NULL;
    }

    return siblings->items[index + 1];
}

struct hayward_window *
window_for_scene_node(struct wlr_scene_node *node) {
    struct wlr_addon *addon = wlr_addon_find(
        &node->addons, &scene_tree_marker_interface,
        &scene_tree_marker_interface
    );
    if (addon == NULL) {
        return NULL;
    }

    struct hayward_window *window;
    window = wl_container_of(addon, window, scene_tree_marker);

    return window;
}
