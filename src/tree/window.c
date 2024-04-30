#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/window.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include <hayward/config.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/list.h>
#include <hayward/profiler.h>
#include <hayward/scene/colours.h>
#include <hayward/scene/nineslice.h>
#include <hayward/scene/text.h>
#include <hayward/server.h>
#include <hayward/theme.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/transaction.h>
#include <hayward/tree/view.h>
#include <hayward/tree/workspace.h>

static void
scene_tree_marker_destroy(struct wlr_addon *addon) {
    // Intentionally left blank.
}

static const struct wlr_addon_interface scene_tree_marker_interface = {
    .name = "hwd_window", .destroy = scene_tree_marker_destroy
};

static void
window_init_scene(struct hwd_window *window) {
    window->scene_tree = wlr_scene_tree_create(NULL);
    assert(window->scene_tree != NULL);
    wlr_addon_init(
        &window->scene_tree_marker, &window->scene_tree->node.addons, &scene_tree_marker_interface,
        &scene_tree_marker_interface
    );

    struct wlr_scene_tree *scene_tree = wlr_scene_tree_create(window->scene_tree);
    window->layers.inner_tree = scene_tree;

    window->layers.titlebar = hwd_nineslice_node_create(scene_tree, NULL, 0, 0, 0, 0);
    assert(window->layers.titlebar != NULL);

    struct hwd_colour text_color = {1.0, 1.0, 1.0, 1.0};
    window->layers.titlebar_text =
        hwd_text_node_create(scene_tree, "", text_color, config->font_description);
    assert(window->layers.titlebar_text != NULL);

    window->layers.titlebar_button_close = &wlr_scene_buffer_create(scene_tree, NULL)->node;

    window->layers.border = hwd_nineslice_node_create(scene_tree, NULL, 0, 0, 0, 0);
    assert(window->layers.border != NULL);

    window->layers.content_tree = wlr_scene_tree_create(scene_tree);
    assert(window->layers.content_tree != NULL);
}

static void
window_update_scene(struct hwd_window *window) {
    double x = window->committed.x;
    double y = window->committed.y;
    double width = window->committed.width;
    double height = window->committed.height;
    double border_left = window->committed.border_left;
    double border_top = window->committed.border_top;
    double titlebar_height = window->committed.titlebar_height;
    bool fullscreen = window->committed.fullscreen;
    bool shaded = window->committed.shaded;

    wlr_scene_node_set_position(&window->layers.inner_tree->node, x, y);

    struct hwd_theme_window *theme = window->committed.theme;

    // Title background.
    wlr_scene_node_set_enabled(window->layers.titlebar, !fullscreen);
    hwd_nineslice_node_update(
        window->layers.titlebar, theme->titlebar.buffer, theme->titlebar.left_break,
        theme->titlebar.right_break, theme->titlebar.top_break, theme->titlebar.bottom_break
    );
    wlr_scene_node_set_position(window->layers.titlebar, 0, 0);
    hwd_nineslice_node_set_size(window->layers.titlebar, width, titlebar_height);

    // Title text.
    wlr_scene_node_set_enabled(window->layers.titlebar_text, !fullscreen);
    wlr_scene_node_set_position(
        window->layers.titlebar_text, theme->titlebar_h_padding, theme->titlebar_v_padding
    );
    hwd_text_node_set_text(window->layers.titlebar_text, window->formatted_title);
    hwd_text_node_set_max_width(
        window->layers.titlebar_text, width - 2 * theme->titlebar_h_padding
    );
    hwd_text_node_set_color(window->layers.titlebar_text, theme->text_colour);

    wlr_scene_node_set_enabled(window->layers.titlebar_button_close, !fullscreen);
    wlr_scene_buffer_set_buffer(
        wlr_scene_buffer_from_node(window->layers.titlebar_button_close), theme->button_close.normal
    );
    wlr_scene_node_set_position(
        window->layers.titlebar_button_close, width - theme->titlebar_h_padding - 16,
        theme->titlebar_v_padding
    );

    // Border.
    wlr_scene_node_set_enabled(window->layers.border, !fullscreen && !shaded);
    hwd_nineslice_node_update(
        window->layers.border, theme->border.buffer, theme->border.left_break,
        theme->border.right_break, theme->border.top_break, theme->border.bottom_break
    );
    wlr_scene_node_set_position(window->layers.border, 0, titlebar_height);
    hwd_nineslice_node_set_size(window->layers.border, width, height - titlebar_height);

    // Content.
    wlr_scene_node_set_enabled(&window->layers.content_tree->node, !shaded);
    wlr_scene_node_set_position(
        &window->layers.content_tree->node, fullscreen ? 0 : border_left,
        fullscreen ? 0 : titlebar_height + border_top
    );

    struct hwd_view *view = window->view;
    if (view->window != window || window->committed.dead) {
        if (view->scene_tree->node.parent == window->layers.content_tree) {
            wlr_scene_node_reparent(&view->scene_tree->node, NULL);
        }
    } else {
        // If the view hasn't responded to the configure, center it within
        // the window. This is important for fullscreen views which
        // refuse to resize to the size of the output.
        if (view->surface) {
            view_center_surface(view);
        }

        wlr_scene_node_reparent(&view->scene_tree->node, window->layers.content_tree);
    }
}

static void
window_destroy_scene(struct hwd_window *window) {
    assert(&window->scene_tree->node.parent->node == NULL);

    wlr_scene_node_destroy(&window->layers.inner_tree->node);

    wlr_addon_finish(&window->scene_tree_marker);
    wlr_scene_node_destroy(&window->scene_tree->node);

    free(window->title);
    free(window->formatted_title);
    wlr_texture_destroy(window->title_focused);
    wlr_texture_destroy(window->title_focused_inactive);
    wlr_texture_destroy(window->title_unfocused);
    wlr_texture_destroy(window->title_urgent);
    wlr_texture_destroy(window->title_focused_tab_title);
}

static bool
window_should_configure(struct hwd_window *window) {
    assert(window != NULL);
    if (window->pending.dead) {
        return false;
    }
    // TODO if the window's view initiated the change, it should not be
    // reconfigured.
    struct hwd_window_state *cstate = &window->committed;
    struct hwd_window_state *nstate = &window->pending;

#if HAVE_XWAYLAND
    // Xwayland views are position-aware and need to be reconfigured
    // when their position changes.
    if (window->view->type == HWD_VIEW_XWAYLAND) {
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
    struct hwd_window *window = wl_container_of(listener, window, transaction_commit);
    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);

    wl_list_remove(&listener->link);
    window->dirty = false;

    wl_signal_add(&transaction_manager->events.apply, &window->transaction_apply);

    if (window->pending.fullscreen != window->committed.fullscreen) {
        if (window->view->impl->set_fullscreen) {
            window->view->impl->set_fullscreen(window->view, window->pending.fullscreen);
        }
        if (window->view->foreign_toplevel) {
            wlr_foreign_toplevel_handle_v1_set_fullscreen(
                window->view->foreign_toplevel, window->pending.fullscreen
            );
        }
    }

    bool hidden = !window->pending.dead && !view_is_visible(window->view);
    if (window_should_configure(window)) {
        struct hwd_window_state *state = &window->pending;

        window->configure_serial = view_configure(
            window->view, state->content_x, state->content_y, state->content_width,
            state->content_height
        );
        if (!hidden) {
            hwd_transaction_manager_acquire_commit_lock(transaction_manager);
            window->is_configuring = true;
        }

        // From here on we are rendering a saved buffer of the view, which
        // means we can send a frame done event to make the client redraw it
        // as soon as possible. Additionally, this is required if a view is
        // mapping and its default geometry doesn't intersect an output.
        view_send_frame_done(window->view);

        view_freeze_buffer(window->view);
    }

    memcpy(&window->committed, &window->pending, sizeof(struct hwd_window_state));
}

static void
window_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_window *window = wl_container_of(listener, window, transaction_apply);
    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);

    wl_list_remove(&listener->link);
    window->is_configuring = false;

    view_unfreeze_buffer(window->view);

    window_update_scene(window);

    if (window->committed.dead) {
        if (window->view->window == window) {
            window->view->window = NULL;
            if (window->view->destroying) {
                view_destroy(window->view);
            }
        }

        wl_signal_add(&transaction_manager->events.after_apply, &window->transaction_after_apply);
    }

    memcpy(&window->current, &window->committed, sizeof(struct hwd_window_state));
}
static void
window_handle_transaction_after_apply(struct wl_listener *listener, void *data) {
    struct hwd_window *window = wl_container_of(listener, window, transaction_after_apply);

    wl_list_remove(&listener->link);

    assert(window->current.dead);

    window_destroy_scene(window);

    free(window);
}

struct hwd_window *
window_create(struct hwd_root *root, struct hwd_view *view) {
    struct hwd_window *window = calloc(1, sizeof(struct hwd_window));
    if (!window) {
        wlr_log(WLR_ERROR, "Unable to allocate hwd_window");
        return NULL;
    }

    static size_t next_id = 1;
    window->id = next_id++;

    wl_signal_init(&window->events.begin_destroy);
    wl_signal_init(&window->events.destroy);

    window->root = root;
    window->view = view;

    window->output_history = create_list();
    window->fullscreen_output_history = create_list();

    window->height_fraction = 1.0;

    window->transaction_commit.notify = window_handle_transaction_commit;
    window->transaction_apply.notify = window_handle_transaction_apply;
    window->transaction_after_apply.notify = window_handle_transaction_after_apply;

    window_init_scene(window);

    window_set_dirty(window);

    return window;
}

bool
window_is_alive(struct hwd_window *window) {
    assert(window != NULL);
    return !window->pending.dead;
}

void
window_begin_destroy(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));

    window_end_mouse_operation(window);

    list_free(window->output_history);
    list_free(window->fullscreen_output_history);

    if (window->parent || window->workspace) {
        window_detach(window);
    }

    wl_signal_emit_mutable(&window->events.begin_destroy, window);

    window_set_dirty(window);
    window->pending.dead = true;
}

void
window_set_dirty(struct hwd_window *window) {
    assert(window != NULL);
    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);

    if (window->dirty) {
        return;
    }

    window->dirty = true;
    wl_signal_add(&transaction_manager->events.commit, &window->transaction_commit);
    hwd_transaction_manager_ensure_queued(transaction_manager);
}

void
window_detach(struct hwd_window *window) {
    struct hwd_column *column = window->parent;
    struct hwd_workspace *workspace = window->workspace;

    if (workspace == NULL) {
        return;
    }

    if (column != NULL) {
        column_remove_child(column, window);
    } else {
        workspace_remove_floating(workspace, window);
    }

    window_set_dirty(window);
}

static void
window_update_theme(struct hwd_window *window) {
    struct hwd_theme *theme = root_get_theme(window->root);
    if (theme == NULL) {
        return;
    }

    struct hwd_theme_window_type *window_type = &theme->floating;
    if (window_is_tiling(window)) {
        if (window_get_previous_sibling(window) == NULL) {
            window_type = &theme->tiled_head;
        } else {
            window_type = &theme->tiled;
        }
    }

    if (view_is_urgent(window->view)) {
        window->pending.theme = &window_type->urgent;
        return;
    }
    if (window->pending.focused) {
        window->pending.theme = &window_type->focused;
        return;
    }

    if (window->parent && window->parent->active_child == window) {
        window->pending.theme = &window_type->active;
        return;
    }

    window->pending.theme = &window_type->inactive;
}

void
window_reconcile_floating(struct hwd_window *window, struct hwd_workspace *workspace) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(workspace != NULL);

    window->workspace = workspace;
    window->parent = NULL;

    window->pending.focused =
        workspace_is_visible(workspace) && workspace_get_active_window(workspace) == window;

    view_set_tiled(window->view, false);

    window_update_theme(window);
    window_set_dirty(window);
}

void
window_reconcile_tiling(struct hwd_window *window, struct hwd_column *column) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(column != NULL);

    struct hwd_workspace *workspace = column->workspace;

    window->workspace = workspace;
    list_clear(window->output_history); // TODO
    list_add(window->output_history, column->output);
    window->parent = column;

    window->pending.focused =
        workspace_is_visible(workspace) && workspace_get_active_window(workspace) == window;

    view_set_tiled(window->view, true);

    window_update_theme(window);
    window_set_dirty(window);
}

void
window_reconcile_detached(struct hwd_window *window) {
    assert(window != NULL);

    window->workspace = NULL;
    window->parent = NULL;

    window->pending.focused = false;

    window_set_dirty(window);
}

void
window_set_moving(struct hwd_window *window, bool moving) {
    if (window->moving == moving) {
        return;
    }
    window->moving = moving;

    if (moving) {
        wlr_scene_node_reparent(&window->layers.inner_tree->node, window->root->layers.moving);
    } else {
        wlr_scene_node_reparent(&window->layers.inner_tree->node, window->scene_tree);
    }
    window_update_theme(window);
}

void
window_arrange(struct hwd_window *window) {
    HWD_PROFILER_TRACE();

    if (window->dirty) {
        struct hwd_window_state *state = &window->pending;

        if (window_is_fullscreen(window)) {
            state->fullscreen = true;

            state->titlebar_height = 0;
            state->border_left = 0;
            state->border_right = 0;
            state->border_top = 0;
            state->border_bottom = 0;
        } else {
            state->fullscreen = false;

            state->titlebar_height = hwd_theme_window_get_titlebar_height(state->theme);
            state->border_left = hwd_theme_window_get_border_left(state->theme);
            state->border_right = hwd_theme_window_get_border_right(state->theme);
            state->border_top = hwd_theme_window_get_border_top(state->theme);
            state->border_bottom = hwd_theme_window_get_border_bottom(state->theme);

            if (window_is_floating(window)) {
                // TODO in all other cases, pending position and size are set by
                // parent.
                struct hwd_output *output = window_get_output(window);
                state->x = output->pending.x + window->floating_x;
                state->y = output->pending.y + window->floating_y;
                state->width = window->floating_width;
                state->height = window->floating_height;
            }
        }

        state->content_x = state->x + state->border_left;
        state->content_y = state->y + state->titlebar_height + state->border_top;
        state->content_width = state->width - state->border_left - state->border_right;
        if (state->content_width < 0) {
            state->content_width = 0;
        }
        state->content_height =
            state->height - state->titlebar_height - state->border_top - state->border_bottom;
        if (state->content_height < 0) {
            state->content_height = 0;
        }
    }
}

void
window_evacuate(struct hwd_window *window, struct hwd_output *old_output) {
    struct hwd_output *new_output = NULL;
    // TODO smarter selection.
    // TODO ignore disabled outputs.
    if (window->root->outputs->length > 1) {
        new_output = window->root->outputs->items[0];
        if (new_output == old_output) {
            new_output = window->root->outputs->items[1];
        }
    }

    if (window_get_fullscreen_output(window) == old_output) {
        list_add(window->fullscreen_output_history, new_output);
    }

    assert(window->output_history->length > 0);
    struct hwd_output *current_output =
        window->output_history->items[window->output_history->length - 1];
    if (current_output != old_output) {
        return;
    }

    struct hwd_column *current_column = window->parent;

    list_add(window->output_history, new_output);

    if (current_column != NULL) {
        struct hwd_workspace *workspace = window->workspace;

        // TODO choose first or last based on relative positions of outputs.
        struct hwd_column *new_column = workspace_get_column_last(workspace, new_output);
        if (new_column == NULL) {
            new_column = column_create();
            workspace_insert_column_last(workspace, new_output, new_column);
        }

        list_add(new_column->children, window);
        window->parent = new_column;
    }

    workspace_set_dirty(window->workspace);
}

void
window_end_mouse_operation(struct hwd_window *window) {
    struct hwd_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) { seatop_unref(seat, window); }
}

bool
window_is_floating(struct hwd_window *window) {
    assert(window != NULL);

    if (window->workspace == NULL) {
        return false;
    }

    if (window->parent != NULL) {
        return false;
    }

    return true;
}

bool
window_is_fullscreen(struct hwd_window *window) {
    return window_get_fullscreen_output(window) != NULL;
}

bool
window_is_tiling(struct hwd_window *window) {
    return window->parent != NULL;
}

void
window_fullscreen(struct hwd_window *window) {
    assert(window != NULL);

    struct hwd_output *output = window_get_output(window);
    window_fullscreen_on_output(window, output);
}

void
window_fullscreen_on_output(struct hwd_window *window, struct hwd_output *output) {
    assert(output != NULL);
    assert(window != NULL);

    if (window_get_fullscreen_output(window) == output) {
        return;
    }

    window_end_mouse_operation(window);

    if (window_get_fullscreen_output(window) != NULL) {
        output_set_dirty(window_get_fullscreen_output(window));
    }

    for (int i = 0; i < window->fullscreen_output_history->length; i++) {
        struct hwd_output *output = window->fullscreen_output_history->items[i];

        list_remove(output->fullscreen_windows, window);
        output_consider_destroy(output);
    }
    list_clear(window->fullscreen_output_history);

    list_add(window->fullscreen_output_history, output);
    list_add(output->fullscreen_windows, window);

    output_set_dirty(output);
    workspace_set_dirty(window->workspace);
    if (window->parent != NULL) {
        column_set_dirty(window->parent);
    }
}

void
window_unfullscreen(struct hwd_window *window) {
    assert(window != NULL);

    window_end_mouse_operation(window);

    for (int i = 0; i < window->fullscreen_output_history->length; i++) {
        struct hwd_output *output = window->fullscreen_output_history->items[i];

        list_remove(output->fullscreen_windows, window);
        output_set_dirty(output);
        output_consider_destroy(output);
    }
    list_clear(window->fullscreen_output_history);

    workspace_set_dirty(window->workspace);
}

void
floating_calculate_constraints(
    struct hwd_window *window, int *min_width, int *max_width, int *min_height, int *max_height
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
    wlr_output_layout_get_box(window->root->output_layout, NULL, &box);

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
floating_natural_resize(struct hwd_window *window) {
    int min_width, max_width, min_height, max_height;
    floating_calculate_constraints(window, &min_width, &max_width, &min_height, &max_height);

    int titlebar_height = hwd_theme_window_get_titlebar_height(window->pending.theme);
    int border_left = hwd_theme_window_get_border_left(window->pending.theme);
    int border_right = hwd_theme_window_get_border_right(window->pending.theme);
    int border_top = hwd_theme_window_get_border_top(window->pending.theme);
    int border_bottom = hwd_theme_window_get_border_bottom(window->pending.theme);

    window->floating_width =
        fmax(min_width, fmin(window->view->natural_width, max_width)) + border_left + border_right;
    window->floating_height = fmax(min_height, fmin(window->view->natural_height, max_height)) +
        titlebar_height + border_top + border_bottom;
    window_set_geometry_from_content(window);
}

void
window_floating_resize_and_center(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));

    struct hwd_output *output = window_get_output(window);
    assert(output != NULL);

    struct wlr_box ob;
    wlr_output_layout_get_box(window->root->output_layout, output->wlr_output, &ob);
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
            window->pending.x =
                output->pending.x + (output->pending.width - window->pending.width) / 2;
            window->pending.y =
                output->pending.y + (output->pending.height - window->pending.height) / 2;
        }
    } else {
        if (window->pending.content_width > output->pending.width ||
            window->pending.content_height > output->pending.height) {
            window->pending.content_x = ob.x + (ob.width - window->pending.content_width) / 2;
            window->pending.content_y = ob.y + (ob.height - window->pending.content_height) / 2;
        } else {
            window->pending.content_x =
                output->pending.x + (output->pending.width - window->pending.content_width) / 2;
            window->pending.content_y =
                output->pending.y + (output->pending.height - window->pending.content_height) / 2;
        }

        window_set_geometry_from_content(window);
    }

    window_set_dirty(window);
}

void
window_floating_set_default_size(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(window->workspace);

    struct hwd_output *output = window_get_output(window);

    int min_width, max_width, min_height, max_height;
    floating_calculate_constraints(window, &min_width, &max_width, &min_height, &max_height);
    struct wlr_box box = {0};
    output_get_box(output, &box);

    double width = fmax(min_width, fmin(box.width * 0.5, max_width));
    double height = fmax(min_height, fmin(box.height * 0.75, max_height));

    window->pending.content_width = width;
    window->pending.content_height = height;
    window_set_geometry_from_content(window);

    window_set_dirty(window);
}

void
window_floating_move_to(
    struct hwd_window *window, struct hwd_output *output, double lx, double ly
) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(window_is_floating(window));

    window->pending.x = lx;
    window->pending.y = ly;
    window->pending.content_x = lx;
    window->pending.content_y = ly;

    window_set_dirty(window);
}

void
window_floating_move_to_center(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(window_is_floating(window));

    struct hwd_output *output = window_get_output(window);

    double new_lx = output->pending.x + (output->pending.width - output->pending.width) / 2;
    double new_ly = output->pending.y + (output->pending.height - output->pending.height) / 2;

    window_floating_move_to(window, output, new_lx, new_ly);
}

struct hwd_output *
window_get_output(struct hwd_window *window) {
    assert(window != NULL);

    struct hwd_output *fullscreen_output = window_get_fullscreen_output(window);
    if (fullscreen_output != NULL) {
        return fullscreen_output;
    }

    assert(window->output_history->length > 0);
    return window->output_history->items[window->output_history->length - 1];
}

struct hwd_output *
window_get_fullscreen_output(struct hwd_window *window) {
    if (window->fullscreen_output_history->length == 0) {
        return NULL;
    }

    return window->fullscreen_output_history->items[window->fullscreen_output_history->length - 1];
}

void
window_get_box(struct hwd_window *window, struct wlr_box *box) {
    assert(window != NULL);

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
window_set_resizing(struct hwd_window *window, bool resizing) {
    if (!window) {
        return;
    }

    if (window->view->impl->set_resizing) {
        window->view->impl->set_resizing(window->view, resizing);
    }
}

void
window_set_geometry_from_content(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(window_is_floating(window));

    struct hwd_window_state *state = &window->pending;

    state->x = state->content_x - state->border_left;
    state->y = state->content_y - state->border_top - state->titlebar_height;
    state->width = state->content_width + state->border_left + state->border_right;
    state->height =
        state->content_height + state->titlebar_height + state->border_top + state->border_bottom;

    window_set_dirty(window);
}

bool
window_is_transient_for(struct hwd_window *child, struct hwd_window *ancestor) {
    return view_is_transient_for(child->view, ancestor->view);
}

void
window_raise_floating(struct hwd_window *window) {
    // Bring window to front by putting it at the end of the floating list.
    if (window->workspace == NULL) {
        return;
    }

    if (!window_is_floating(window)) {
        return;
    }

    list_move_to_end(window->workspace->floating, window);

    workspace_set_dirty(window->workspace);
}

list_t *
window_get_siblings(struct hwd_window *window) {
    if (window_is_tiling(window)) {
        return window->parent->children;
    }
    if (window_is_floating(window)) {
        return window->workspace->floating;
    }
    return NULL;
}

int
window_sibling_index(struct hwd_window *window) {
    return list_find(window_get_siblings(window), window);
}

struct hwd_window *
window_get_previous_sibling(struct hwd_window *window) {
    if (!window->parent) {
        return NULL;
    }

    list_t *siblings = window->parent->children;
    int index = list_find(siblings, window);
    assert(index != -1);

    if (index == 0) {
        return NULL;
    }

    return siblings->items[index - 1];
}

struct hwd_window *
window_get_next_sibling(struct hwd_window *window) {
    assert(window != NULL);

    if (!window->parent) {
        return NULL;
    }

    list_t *siblings = window->parent->children;
    int index = list_find(siblings, window);
    assert(index != -1);

    if (index == siblings->length - 1) {
        return NULL;
    }

    return siblings->items[index + 1];
}

struct hwd_window *
window_for_scene_node(struct wlr_scene_node *node) {
    struct wlr_addon *addon =
        wlr_addon_find(&node->addons, &scene_tree_marker_interface, &scene_tree_marker_interface);
    if (addon == NULL) {
        return NULL;
    }

    struct hwd_window *window;
    window = wl_container_of(addon, window, scene_tree_marker);

    return window;
}
