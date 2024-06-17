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
#include <wlr/types/wlr_compositor.h>
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
window_freeze_content_iterator(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct wlr_scene_tree *tree = data;

    struct wlr_scene_buffer *sbuf = wlr_scene_buffer_create(tree, NULL);
    assert(sbuf != NULL);

    wlr_scene_buffer_set_dest_size(sbuf, buffer->dst_width, buffer->dst_height);
    wlr_scene_buffer_set_opaque_region(sbuf, &buffer->opaque_region);
    wlr_scene_buffer_set_source_box(sbuf, &buffer->src_box);
    wlr_scene_node_set_position(&sbuf->node, sx, sy);
    wlr_scene_buffer_set_transform(sbuf, buffer->transform);
    wlr_scene_buffer_set_buffer(sbuf, buffer->buffer);
}

static void
window_freeze_content(struct hwd_window *window) {
    assert(window->layers.saved_content_tree == NULL);

    window->layers.saved_content_tree = wlr_scene_tree_create(window->scene_tree);
    assert(window->layers.saved_content_tree != NULL);

    // Enable and disable the saved surface tree like so to atomitaclly update
    // the tree. This will prevent over damaging or other weirdness.
    wlr_scene_node_set_enabled(&window->layers.saved_content_tree->node, false);

    wlr_scene_node_for_each_buffer(
        &window->layers.content_tree->node, window_freeze_content_iterator,
        window->layers.saved_content_tree
    );

    wlr_scene_node_set_enabled(&window->layers.content_tree->node, false);
    wlr_scene_node_set_enabled(&window->layers.saved_content_tree->node, true);
}

static void
window_unfreeze_content(struct hwd_window *window) {
    if (window->layers.saved_content_tree == NULL) {
        return;
    }

    wlr_scene_node_destroy(&window->layers.saved_content_tree->node);
    window->layers.saved_content_tree = NULL;

    wlr_scene_node_set_enabled(&window->layers.content_tree->node, true);
}

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

    window->layers.popup_tree = wlr_scene_tree_create(scene_tree);
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
    hwd_text_node_set_text(window->layers.titlebar_text, window->title);
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

    wlr_scene_node_set_position(
        &window->layers.popup_tree->node, fullscreen ? 0 : border_left,
        fullscreen ? 0 : titlebar_height + border_top
    );
}

static void
window_destroy_scene(struct hwd_window *window) {
    assert(&window->scene_tree->node.parent->node == NULL);

    // Unparent content tree rather than destroying to allow view to do something
    // else with it.
    struct wl_list *link = &window->layers.content_tree->children;
    while (link->next != &window->layers.content_tree->children) {
        struct wlr_scene_node *old_node = wl_container_of(link, old_node, link);
        link = link->next;
        wlr_scene_node_reparent(old_node, NULL);
    }

    wlr_scene_node_destroy(&window->layers.inner_tree->node);

    wlr_addon_finish(&window->scene_tree_marker);
    wlr_scene_node_destroy(&window->scene_tree->node);

    free(window->title);
    wlr_texture_destroy(window->title_focused);
    wlr_texture_destroy(window->title_focused_inactive);
    wlr_texture_destroy(window->title_unfocused);
    wlr_texture_destroy(window->title_urgent);
    wlr_texture_destroy(window->title_focused_tab_title);
}

void
window_begin_configure(struct hwd_window *window) {
    if (window->is_configuring) {
        return;
    }

    window_freeze_content(window);

    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);
    hwd_transaction_manager_acquire_commit_lock(transaction_manager);

    window->is_configuring = true;
}

void
window_end_configure(struct hwd_window *window) {
    if (!window->is_configuring) {
        return;
    }
    window->is_configuring = false;

    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);
    hwd_transaction_manager_release_commit_lock(transaction_manager);
}

static void
window_handle_parent_begin_destroy(struct wl_listener *listener, void *data) {
    struct hwd_window *window = wl_container_of(listener, window, parent_begin_destroy);

    window->parent = NULL;
    wl_list_remove(&window->parent_begin_destroy.link);
}

static void
window_handle_transaction_commit(struct wl_listener *listener, void *data) {
    struct hwd_window *window = wl_container_of(listener, window, transaction_commit);
    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);

    wl_list_remove(&listener->link);
    window->dirty = false;

    wl_signal_add(&transaction_manager->events.apply, &window->transaction_apply);

    wl_signal_emit_mutable(&window->events.commit, window);

    memcpy(&window->committed, &window->pending, sizeof(struct hwd_window_state));
}

static void
window_handle_transaction_apply(struct wl_listener *listener, void *data) {
    struct hwd_window *window = wl_container_of(listener, window, transaction_apply);
    struct hwd_transaction_manager *transaction_manager =
        root_get_transaction_manager(window->root);

    wl_list_remove(&listener->link);
    window->is_configuring = false;

    window_unfreeze_content(window);

    window_update_scene(window);

    if (window->committed.dead) {
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

    wl_signal_init(&window->events.commit);
    wl_signal_init(&window->events.close);
    wl_signal_init(&window->events.begin_destroy);
    wl_signal_init(&window->events.destroy);

    window->root = root;
    window->view = view;

    window->output_history = create_list();

    window->height_fraction = 1.0;

    window->maximum_width = INFINITY;
    window->maximum_height = INFINITY;

    window->transaction_commit.notify = window_handle_transaction_commit;
    window->transaction_apply.notify = window_handle_transaction_apply;
    window->transaction_after_apply.notify = window_handle_transaction_after_apply;

    window_init_scene(window);

    window_set_dirty(window);

    return window;
}

void
window_close(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));

    wl_signal_emit_mutable(&window->events.close, window);
}

bool
window_is_alive(struct hwd_window *window) {
    assert(window != NULL);
    return !window->dead;
}

void
window_begin_destroy(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));

    window_end_mouse_operation(window);

    window_detach(window);

    for (int i = 0; i < window->output_history->length; i++) {
        struct hwd_output *output = window->output_history->items[i];
        if (window->fullscreen) {
            list_remove(output->fullscreen_windows, window);
            output_set_dirty(output); // TODO
        }
    }
    list_free(window->output_history);

    if (window->urgent_timer) {
        wl_event_source_remove(window->urgent_timer);
        window->urgent_timer = NULL;
    }

    if (window->parent != NULL) {
        window->parent = NULL;
        wl_list_remove(&window->parent_begin_destroy.link);
    }

    wl_signal_emit_mutable(&window->events.begin_destroy, window);

    window_set_dirty(window);
    window->dead = true;
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
    struct hwd_column *column = window->column;
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

static struct hwd_theme_window *
window_get_theme(struct hwd_window *window) {
    struct hwd_theme *theme = root_get_theme(window->root);
    if (theme == NULL) {
        return NULL;
    }

    struct hwd_theme_window_type *window_type = &theme->floating;
    if (window_is_tiling(window)) {
        if (window_get_previous_sibling(window) == NULL) {
            window_type = &theme->tiled_head;
        } else {
            window_type = &theme->tiled;
        }
    }

    if (window->is_urgent) {
        return &window_type->urgent;
    }

    if (workspace_get_active_window(window->workspace) == window) {
        return &window_type->focused;
    }

    if (window->column && window->column->active_child == window) {
        return &window_type->active;
    }

    return &window_type->inactive;
}

void
window_reconcile_floating(struct hwd_window *window, struct hwd_workspace *workspace) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(workspace != NULL);

    window->workspace = workspace;
    window->column = NULL;

    if (!(window->floating_width > 0 && window->floating_height > 0)) {
        window->floating_width = window->natural_width;
        window->floating_width =
            fmax(window->floating_width, window->minimum_width > 0 ? window->minimum_width : 25);
        window->floating_width = fmin(
            window->floating_width, window->maximum_width > 0 ? window->maximum_width : INFINITY
        );

        window->floating_height = window->natural_height;
        window->floating_height =
            fmax(window->floating_height, window->minimum_height > 0 ? window->minimum_height : 25);
        window->floating_height = fmin(
            window->floating_height, window->maximum_height > 0 ? window->maximum_height : INFINITY
        );

        // TODO position to avoid other windows.
        window->floating_x = 0.5;
        window->floating_y = 0.5;
    }

    window_set_dirty(window);
}

void
window_reconcile_tiling(struct hwd_window *window, struct hwd_column *column) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(column != NULL);

    struct hwd_workspace *workspace = column->workspace;

    window->workspace = workspace;
    window->column = column;

    list_clear(window->output_history);
    list_add(window->output_history, column->output);
    window->output = column->output;

    window_set_dirty(window);
}

void
window_reconcile_detached(struct hwd_window *window) {
    assert(window != NULL);

    window->workspace = NULL;
    window->column = NULL;

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

    window_set_dirty(window);
}

void
window_arrange(struct hwd_window *window) {
    HWD_PROFILER_TRACE();

    if (window->dirty) {
        struct hwd_window_state *state = &window->pending;

        state->dead = window->dead;

        state->focused =
            (!window->dead && workspace_is_visible(window->workspace) &&
             workspace_get_active_window(window->workspace) == window);

        state->theme = window_get_theme(window);

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

                double center_x = output->pending.x + (window->floating_x * output->pending.width);
                double center_y = output->pending.y + (window->floating_y * output->pending.height);

                state->x = center_x - (window->floating_width / 2) - state->border_left;
                state->y = center_y - (window->floating_height / 2) - state->titlebar_height -
                    state->border_top;
                state->width = window->floating_width + state->border_left + state->border_right;
                state->height = window->floating_height + state->titlebar_height +
                    state->border_top + state->border_bottom;
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
    if (window_get_output(window) != old_output) {
        return;
    }

    // Find the most earliest enabled output in history.
    struct hwd_output *new_output = NULL;
    for (int i = 0; i < window->output_history->length; i++) {
        struct hwd_output *candidate_output = window->output_history->items[i];
        if (candidate_output->enabled) {
            new_output = candidate_output;
            break;
        }
    }

    // Fallback to picking a new output using "heuristics" (TODO).
    if (new_output == NULL) {
        for (int i = 0; i < window->root->outputs->length; i++) {
            struct hwd_output *candidate_output = window->output_history->items[i];
            if (candidate_output->enabled) {
                new_output = candidate_output;
                list_add(window->output_history, new_output);
                break;
            }
        }
    }

    assert(new_output != NULL);

    // Select a column in the new output if tiling.
    if (window->column && !window->fullscreen) {
        struct hwd_workspace *workspace = window->workspace;

        struct hwd_column *new_column = NULL;

        // TODO choose first or last based on relative positions of outputs.
        for (int i = 0; i < workspace->columns->length; i++) {
            struct hwd_column *column = workspace->columns->items[i];
            if (column->output != new_output) {
                continue;
            }

            new_column = column;

            if (list_find(column->children, window) != -1) {
                break;
            }
        }

        if (new_column == NULL) {
            new_column = column_create();
            workspace_insert_column_last(workspace, new_output, new_column);
        }

        assert(new_column != NULL);

        if (list_find(new_column->children, window) == -1) {
            list_add(new_column->children, window);
        }

        column_set_dirty(new_column);
    }

    if (window->fullscreen) {
        if (list_find(new_output->fullscreen_windows, window) == -1) {
            list_add(new_output->fullscreen_windows, window);
        }
    }

    window->output = new_output;
    window_set_dirty(window);
    output_set_dirty(new_output);
}

void
window_end_mouse_operation(struct hwd_window *window) {
    struct hwd_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) { seatop_unref(seat, window); }
}

void
window_set_title(struct hwd_window *window, const char *title) {
    assert(window != NULL);
    assert(title != NULL);

    free(window->title);
    window->title = strdup(title);

    window_set_dirty(window);
}

void
window_set_natural_size(struct hwd_window *window, double width, double height) {
    assert(window != NULL);
    assert(window_is_alive(window));

    if (window->natural_width != width) {
        window->natural_width = width;
        window_set_dirty(window);
    }
    if (window->natural_height != height) {
        window->natural_height = height;
        window_set_dirty(window);
    }
}

void
window_set_minimum_size(struct hwd_window *window, double min_width, double min_height) {
    assert(window != NULL);
    assert(window_is_alive(window));

    if (window->minimum_width != min_width) {
        window->minimum_width = min_width;
        window_set_dirty(window);
    }
    if (window->minimum_height != min_height) {
        window->minimum_height = min_height;
        window_set_dirty(window);
    }
}

void
window_set_maximum_size(struct hwd_window *window, double max_width, double max_height) {
    assert(window != NULL);
    assert(window_is_alive(window));

    if (window->maximum_width != max_width) {
        window->maximum_width = max_width;
        window_set_dirty(window);
    }
    if (window->maximum_height != max_height) {
        window->maximum_height = max_height;
        window_set_dirty(window);
    }
}

void
window_set_urgent(struct hwd_window *window, bool urgent) {
    assert(window != NULL);
    assert(window_is_alive(window));

    if (window->is_urgent == urgent) {
        return;
    }

    if (urgent && root_get_focused_window(window->root) == window) {
        return;
    }

    if (urgent && window->urgent_timer) {
        wl_event_source_remove(window->urgent_timer);
        window->urgent_timer = NULL;
    }

    window->is_urgent = urgent;
}

void
window_set_transient_for(struct hwd_window *window, struct hwd_window *parent) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(parent == NULL || window_is_alive(parent));

    if (window->parent != NULL) {
        wl_list_remove(&window->parent_begin_destroy.link);
        window->parent = NULL;
    }

    if (parent != NULL) {
        window->parent = parent;
        window->parent_begin_destroy.notify = window_handle_parent_begin_destroy;
        wl_signal_add(&window->parent->events.begin_destroy, &window->parent_begin_destroy);
    }
}

void
window_set_content(struct hwd_window *window, struct wlr_scene_node *new_node) {
    assert(window != NULL);
    assert(window_is_alive(window));

    struct wl_list *link = &window->layers.content_tree->children;
    while (link->next != &window->layers.content_tree->children) {
        struct wlr_scene_node *old_node = wl_container_of(link, old_node, link);
        link = link->next;
        wlr_scene_node_reparent(old_node, NULL);
    }

    wlr_scene_node_reparent(new_node, window->layers.content_tree);
}

void
window_add_popup(struct hwd_window *window, struct wlr_surface *surface) {
    assert(window != NULL);
    assert(window_is_alive(window));
    assert(surface != NULL);

    wlr_scene_surface_create(window->layers.popup_tree, surface);
}

void
window_set_popup_position(struct hwd_window *window, struct wlr_surface *popup, int x, int y) {
    wlr_log(WLR_ERROR, "Position %p: %ix%i", (void *)popup, x, y);
    struct wlr_scene_node *popup_node = NULL;
    wl_list_for_each(popup_node, &window->layers.popup_tree->children, link) {
        struct wlr_scene_buffer *popup_buffer = wlr_scene_buffer_from_node(popup_node);
        struct wlr_scene_surface *popup_scene_surface =
            wlr_scene_surface_try_from_buffer(popup_buffer);

        if (popup_scene_surface->surface == popup) {
            wlr_scene_node_set_position(popup_node, x, y);
            return;
        }
    }
}

void
window_remove_popup(struct hwd_window *window, struct wlr_surface *popup) {
    struct wlr_scene_node *popup_node = NULL;
    wl_list_for_each(popup_node, &window->layers.popup_tree->children, link) {
        struct wlr_scene_buffer *popup_buffer = wlr_scene_buffer_from_node(popup_node);
        struct wlr_scene_surface *popup_scene_surface =
            wlr_scene_surface_try_from_buffer(popup_buffer);

        if (popup_scene_surface->surface == popup) {
            wlr_scene_node_destroy(popup_node);
            return;
        }
    }
}

void
window_request_activate(struct hwd_window *window) {
    assert(window != NULL);
    assert(window_is_alive(window));

    switch (config->focus_on_window_activation) {
    case FOWA_SMART:
        if (workspace_is_visible(window->workspace)) {
            root_set_focused_window(window->root, window);
        } else {
            window_set_urgent(window, true);
        }
        break;
    case FOWA_URGENT:
        window_set_urgent(window, true);
        break;
    case FOWA_FOCUS:
        root_set_focused_window(window->root, window);
        break;
    case FOWA_NONE:
        break;
    }
}

bool
window_is_floating(struct hwd_window *window) {
    assert(window != NULL);

    if (window->workspace == NULL) {
        return false;
    }

    if (window->column != NULL) {
        return false;
    }

    return true;
}

bool
window_is_fullscreen(struct hwd_window *window) {
    return window->fullscreen;
}

bool
window_is_tiling(struct hwd_window *window) {
    return window->column != NULL;
}

bool
window_is_transient_for(struct hwd_window *child, struct hwd_window *ancestor) {
    for (struct hwd_window *candidate = child->parent; candidate != NULL;
         candidate = candidate->parent) {
        if (candidate == ancestor) {
            return true;
        }
    }
    return false;
}

bool
window_is_visible(struct hwd_window *window) {

    if (window->dead) {
        return false;
    }
    struct hwd_workspace *workspace = window->workspace;
    if (!workspace) {
        return false;
    }

    struct hwd_output *output = window_get_output(window);
    if (!output) {
        return false;
    }

    // Check view isn't in a shaded window.
    struct hwd_column *column = window->column;
    if (column != NULL && window->pending.shaded) {
        return false;
    }

    // Check view isn't hidden by another fullscreen view
    struct hwd_window *fs = workspace_get_fullscreen_window_for_output(workspace, output);
    if (fs && !window_is_fullscreen(window) && !window_is_transient_for(window, fs)) {
        return false;
    }
    return true;
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

    struct hwd_output *current_output = window_get_output(window);

    if (window_is_fullscreen(window) && output == current_output) {
        return;
    }

    window_end_mouse_operation(window);

    if (window_is_fullscreen(window)) {
        list_remove(output->fullscreen_windows, window);
        output_set_dirty(current_output);
    }

    workspace_set_dirty(window->workspace);
    if (window->column != NULL) {
        column_set_dirty(window->column);
    }

    if (output != current_output) {
        list_remove(window->output_history, output);
        int current_output_index = list_find(window->output_history, current_output);
        assert(current_output_index != -1);
        list_insert(window->output_history, current_output_index, output);
        window->output = output;
    }

    list_add(output->fullscreen_windows, window);
    output_set_dirty(output);

    window->fullscreen = true;
}

void
window_unfullscreen(struct hwd_window *window) {
    assert(window != NULL);

    if (!window_is_fullscreen(window)) {
        return;
    }

    window_end_mouse_operation(window);

    for (int i = 0; i < window->root->outputs->length; i++) {
        struct hwd_output *output = window->root->outputs->items[i];
        list_remove(output->fullscreen_windows, window);
    }

    if (window->column != NULL) {
        if (window->column->output != window_get_output(window)) {
            list_remove(window->output_history, window->column->output);
            int current_output_index = list_find(window->output_history, window->output);
            list_insert(window->output_history, current_output_index, window->column->output);
            window->output = window->column->output;
        }

        if (!window->column->output->enabled) {
            // TODO pick better column.
        }

        // TODO set window as active in column if has focus.

        column_set_dirty(window->column);
    } else {
        // TODO raise window if has focus.

        workspace_set_dirty(window->workspace);
    }

    window->fullscreen = false;
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
    if (window->minimum_width != 0) {
        *min_width = fmax(*min_width, window->minimum_width);
    }

    if (config->floating_minimum_height == -1) { // no minimum
        *min_height = 0;
    } else if (config->floating_minimum_height == 0) { // automatic
        *min_height = 50;
    } else {
        *min_height = config->floating_minimum_height;
    }
    if (window->minimum_height != 0) {
        *min_height = fmax(*min_height, window->minimum_height);
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
    if (window->maximum_width != 0) {
        *max_width = fmin(*max_width, window->maximum_width);
    }

    if (config->floating_maximum_height == -1) { // no maximum
        *max_height = INT_MAX;
    } else if (config->floating_maximum_height == 0) { // automatic
        *max_height = box.height;
    } else {
        *max_height = config->floating_maximum_height;
    }
    if (window->maximum_height != 0) {
        *max_height = fmin(*max_height, window->maximum_height);
    }
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
    return window->output;
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
    assert(window != NULL);
    assert(window_is_alive(window));

    window->resizing = true;

    window_set_dirty(window);
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

struct hwd_window *
window_get_previous_sibling(struct hwd_window *window) {
    if (!window->column) {
        return NULL;
    }

    list_t *siblings = window->column->children;
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

    if (!window->column) {
        return NULL;
    }

    list_t *siblings = window->column->children;
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
