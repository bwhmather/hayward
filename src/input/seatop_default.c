#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/input/seatop_default.h"

#include <assert.h>
#include <float.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-protocol.h>
#include <wayland-util.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>
#include <wlr/xwayland/xwayland.h>

#include <hayward/config.h>
#include <hayward/desktop/xwayland.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input_manager.h>
#include <hayward/input/seat.h>
#include <hayward/input/seatop_down.h>
#include <hayward/input/seatop_move.h>
#include <hayward/input/seatop_resize_floating.h>
#include <hayward/input/seatop_resize_tiling.h>
#include <hayward/input/tablet.h>
#include <hayward/list.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/output.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

struct seatop_default_event {
    struct hwd_window *previous_window;
    uint32_t pressed_buttons[HWD_CURSOR_PRESSED_BUTTONS_CAP];
    size_t pressed_button_count;
};

/*-----------------------------------------\
 * Functions shared by multiple callbacks  /
 *---------------------------------------*/

static bool
column_edge_is_external(struct hwd_column *column, enum wlr_edges edge) {
    if (edge == WLR_EDGE_TOP) {
        return true;
    }

    if (edge == WLR_EDGE_BOTTOM) {
        return true;
    }

    struct hwd_workspace *workspace = column->pending.workspace;
    struct hwd_output *output = column->pending.output;

    if (edge == WLR_EDGE_LEFT) {
        struct hwd_column *first_column = workspace_get_column_first(workspace, output);
        return column == first_column;
    }

    if (edge == WLR_EDGE_RIGHT) {
        struct hwd_column *last_column = workspace_get_column_last(workspace, output);
        return column == last_column;
    }

    return false;
}

static bool
window_edge_is_external(struct hwd_window *window, enum wlr_edges edge) {
    assert(window_is_tiling(window));

    if (edge == WLR_EDGE_LEFT || edge == WLR_EDGE_RIGHT) {
        return column_edge_is_external(window->parent, edge);
    }

    list_t *siblings = window_get_siblings(window);
    int index = list_find(siblings, window);
    assert(index >= 0);

    if (edge == WLR_EDGE_TOP && index == 0) {
        return true;
    }

    if (edge == WLR_EDGE_BOTTOM && index == siblings->length - 1) {
        return true;
    }

    return false;
}

static enum wlr_edges
find_edge(struct hwd_window *window, struct wlr_surface *surface, struct hwd_cursor *cursor) {
    if (surface && window->view->surface != surface) {
        return WLR_EDGE_NONE;
    }
    if (window_is_fullscreen(window)) {
        return WLR_EDGE_NONE;
    }

    enum wlr_edges edge = 0;
    if (cursor->cursor->x < window->pending.x + window->pending.border_left) {
        edge |= WLR_EDGE_LEFT;
    }
    if (cursor->cursor->y < window->pending.y + window->pending.border_top) {
        edge |= WLR_EDGE_TOP;
    }
    if (cursor->cursor->x >=
        window->pending.x + window->pending.width - window->pending.border_right) {
        edge |= WLR_EDGE_RIGHT;
    }
    if (cursor->cursor->y >=
        window->pending.y + window->pending.height - window->pending.border_bottom) {
        edge |= WLR_EDGE_BOTTOM;
    }

    return edge;
}

/**
 * If the cursor is over a _resizable_ edge, return the edge.
 * Edges that can't be resized are edges of the workspace.
 */
static enum wlr_edges
find_resize_edge(
    struct hwd_window *window, struct wlr_surface *surface, struct hwd_cursor *cursor
) {
    struct hwd_column *column = window->parent;

    enum wlr_edges edge = find_edge(window, surface, cursor);

    if (window_is_floating(window)) {
        return edge;
    }

    if (window_edge_is_external(window, edge)) {
        return WLR_EDGE_NONE;
    }

    if (column->pending.layout == L_STACKED && (edge == WLR_EDGE_TOP || edge == WLR_EDGE_BOTTOM)) {
        return WLR_EDGE_NONE;
    }

    return edge;
}

static void
cursor_update_image(struct hwd_cursor *cursor, struct hwd_window *window) {
    if (window != NULL && window_is_alive(window)) {
        // Try a window's resize edge
        enum wlr_edges edge = find_resize_edge(window, NULL, cursor);
        if (edge == WLR_EDGE_NONE) {
            cursor_set_image(cursor, "left_ptr", NULL);
        } else if (window_is_floating(window)) {
            cursor_set_image(cursor, wlr_xcursor_get_resize_name(edge), NULL);
        } else {
            if (edge & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) {
                cursor_set_image(cursor, "col-resize", NULL);
            } else {
                cursor_set_image(cursor, "row-resize", NULL);
            }
        }
    } else {
        cursor_set_image(cursor, "left_ptr", NULL);
    }
}

/**
 * Return the mouse binding which matches modifier, click location, release,
 * and pressed button state, otherwise return null.
 */
static struct hwd_binding *
get_active_mouse_binding(
    struct seatop_default_event *e, list_t *bindings, uint32_t modifiers, bool release,
    bool on_titlebar, bool on_border, bool on_content, bool on_workspace, const char *identifier
) {
    uint32_t click_region = ((on_titlebar || on_workspace) ? BINDING_TITLEBAR : 0) |
        ((on_border || on_workspace) ? BINDING_BORDER : 0) |
        ((on_content || on_workspace) ? BINDING_CONTENTS : 0);

    struct hwd_binding *current = NULL;
    for (int i = 0; i < bindings->length; ++i) {
        struct hwd_binding *binding = bindings->items[i];
        if (modifiers ^ binding->modifiers ||
            e->pressed_button_count != (size_t)binding->keys->length ||
            release != (binding->flags & BINDING_RELEASE) || !(click_region & binding->flags) ||
            (on_workspace && (click_region & binding->flags) != click_region) ||
            (strcmp(binding->input, identifier) != 0 && strcmp(binding->input, "*") != 0)) {
            continue;
        }

        bool match = true;
        for (size_t j = 0; j < e->pressed_button_count; j++) {
            uint32_t key = *(uint32_t *)binding->keys->items[j];
            if (key != e->pressed_buttons[j]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }

        if (!current || strcmp(current->input, "*") == 0) {
            current = binding;
            if (strcmp(current->input, identifier) == 0) {
                // If a binding is found for the exact input, quit searching
                break;
            }
        }
    }
    return current;
}

/**
 * Remove a button (and duplicates) from the sorted list of currently pressed
 * buttons.
 */
static void
state_erase_button(struct seatop_default_event *e, uint32_t button) {
    size_t j = 0;
    for (size_t i = 0; i < e->pressed_button_count; ++i) {
        if (i > j) {
            e->pressed_buttons[j] = e->pressed_buttons[i];
        }
        if (e->pressed_buttons[i] != button) {
            ++j;
        }
    }
    while (e->pressed_button_count > j) {
        --e->pressed_button_count;
        e->pressed_buttons[e->pressed_button_count] = 0;
    }
}

/**
 * Add a button to the sorted list of currently pressed buttons, if there
 * is space.
 */
static void
state_add_button(struct seatop_default_event *e, uint32_t button) {
    if (e->pressed_button_count >= HWD_CURSOR_PRESSED_BUTTONS_CAP) {
        return;
    }
    size_t i = 0;
    while (i < e->pressed_button_count && e->pressed_buttons[i] < button) {
        ++i;
    }
    size_t j = e->pressed_button_count;
    while (j > i) {
        e->pressed_buttons[j] = e->pressed_buttons[j - 1];
        --j;
    }
    e->pressed_buttons[i] = button;
    e->pressed_button_count++;
}

/*-------------------------------------------\
 * Functions used by handle_tablet_tool_tip  /
 *-----------------------------------------*/

static void
handle_tablet_tool_tip(
    struct hwd_seat *seat, struct hwd_tablet_tool *tool, uint32_t time_msec,
    enum wlr_tablet_tool_tip_state state
) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
        return;
    }

    struct hwd_cursor *cursor = seat->cursor;
    struct hwd_output *output = NULL;
    struct hwd_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx, sy;

    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface, &sx, &sy
    );

    assert(surface);

    struct wlr_layer_surface_v1 *layer = wlr_layer_surface_v1_try_from_wlr_surface(surface);
#if HAVE_XWAYLAND
    struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
#endif

    if (layer != NULL) {
        // Handle tapping a layer surface.

        if (layer->current.keyboard_interactive) {
            root_set_focused_layer(root, layer);
        }
    } else if (window != NULL) {
        bool is_floating_or_child = window_is_floating(window);
        bool is_fullscreen_or_child = window_is_fullscreen(window);
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
        bool mod_pressed =
            keyboard && (wlr_keyboard_get_modifiers(keyboard) & config->floating_mod);

        // Handle beginning floating move.
        if (is_floating_or_child && !is_fullscreen_or_child && mod_pressed) {
            root_set_focused_window(root, window);
            seatop_begin_move(seat, window);
            root_commit_focus(root);
            return;
        }

        // Handle moving a tiled window.
        if (mod_pressed && !is_floating_or_child && !window_is_fullscreen(window)) {
            seatop_begin_move(seat, window);
            return;
        }

        // Handle tapping on a window surface.
        root_set_focused_window(root, window);
        seatop_begin_down(seat, window, time_msec, sx, sy);
        root_commit_focus(root);
    }
#if HAVE_XWAYLAND
    // Handle tapping on an xwayland unmanaged view
    else if (xsurface != NULL) {
        if (xsurface->override_redirect && wlr_xwayland_or_surface_wants_focus(xsurface)) {
            struct wlr_xwayland *xwayland = server.xwayland->xwayland;

            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
            root_set_focused_surface(root, xsurface->surface);
        }
    }
#endif

    wlr_tablet_v2_tablet_tool_notify_down(tool->tablet_v2_tool);
    wlr_tablet_tool_v2_start_implicit_grab(tool->tablet_v2_tool);
}

/*----------------------------------\
 * Functions used by handle_button  /
 *--------------------------------*/

static bool
trigger_pointer_button_binding(
    struct hwd_seat *seat, struct wlr_input_device *device, uint32_t button,
    enum wl_pointer_button_state state, uint32_t modifiers, bool on_titlebar, bool on_border,
    bool on_contents, bool on_workspace
) {
    // We can reach this for non-pointer devices if we're currently emulating
    // pointer input for one. Emulated input should not trigger bindings. The
    // device can be NULL if this is synthetic (e.g. haywardmsg-generated)
    // input.
    if (device && device->type != WLR_INPUT_DEVICE_POINTER) {
        return false;
    }

    struct seatop_default_event *e = seat->seatop_data;

    char *device_identifier = device ? input_device_get_identifier(device) : strdup("*");
    struct hwd_binding *binding = NULL;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        state_add_button(e, button);
        binding = get_active_mouse_binding(
            e, config->current_mode->mouse_bindings, modifiers, false, on_titlebar, on_border,
            on_contents, on_workspace, device_identifier
        );
    } else {
        binding = get_active_mouse_binding(
            e, config->current_mode->mouse_bindings, modifiers, true, on_titlebar, on_border,
            on_contents, on_workspace, device_identifier
        );
        state_erase_button(e, button);
    }

    free(device_identifier);
    if (binding) {
        seat_execute_command(seat, binding);
        return true;
    }

    return false;
}

static void
handle_button(
    struct hwd_seat *seat, uint32_t time_msec, struct wlr_input_device *device, uint32_t button,
    enum wl_pointer_button_state state
) {
    struct hwd_cursor *cursor = seat->cursor;

    // Determine what's under the cursor.
    struct hwd_output *output;
    struct hwd_window *window;
    struct wlr_surface *surface = NULL;
    double sx, sy;

    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface, &sx, &sy
    );

    struct hwd_workspace *workspace = root_get_active_workspace(root);

    bool is_floating = window && window_is_floating(window);
    bool is_fullscreen = window && window_is_fullscreen(window);
    enum wlr_edges edge = window ? find_edge(window, surface, cursor) : WLR_EDGE_NONE;
    enum wlr_edges resize_edge =
        window && edge ? find_resize_edge(window, surface, cursor) : WLR_EDGE_NONE;
    bool on_border = edge != WLR_EDGE_NONE;
    bool on_contents = window && !on_border && surface;
    bool on_workspace = output && !window && !surface;
    bool on_titlebar = window && !on_border && !surface;

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
    uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

    // Handle mouse bindings
    if (trigger_pointer_button_binding(
            seat, device, button, state, modifiers, on_titlebar, on_border, on_contents,
            on_workspace
        )) {
        return;
    }

    // Handle clicking an empty workspace
    if (output && !window && !surface) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
            workspace_set_active_window(workspace, NULL);
            root_commit_focus(root);
        }
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

    // Handle clicking a layer surface
    if (surface != NULL) {
        struct wlr_layer_surface_v1 *layer = wlr_layer_surface_v1_try_from_wlr_surface(surface);
        if (layer != NULL) {
            if (layer->current.keyboard_interactive) {
                root_set_focused_layer(root, layer);
            }
            if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
                seatop_begin_down_on_surface(seat, surface, time_msec, sx, sy);
            }
            root_commit_focus(root);
            seat_pointer_notify_button(seat, time_msec, button, state);
            return;
        }
    }

    // Handle tiling resize via border
    if (window && resize_edge && button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED &&
        !is_floating) {
        // If a resize is triggered on a window within a stacked
        // column, change focus to the tab which already had inactive
        // focus -- otherwise, if the user clicked on the title of a
        // hidden tab, we'd change the active tab when the user
        // probably just wanted to resize.
        struct hwd_window *window_to_focus = window;
        root_set_focused_window(root, window_to_focus);

        seatop_begin_resize_tiling(
            seat, window, edge
        ); // TODO (hayward) will only ever take a window.

        root_commit_focus(root);
        return;
    }

    // Handle tiling resize via mod
    bool mod_pressed = modifiers & config->floating_mod;
    if (window && !is_floating && mod_pressed && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        uint32_t btn_resize = config->floating_mod_inverse ? BTN_LEFT : BTN_RIGHT;
        if (button == btn_resize) {
            edge = 0;
            edge |= cursor->cursor->x > window->pending.x + window->pending.width / 2
                ? WLR_EDGE_RIGHT
                : WLR_EDGE_LEFT;
            edge |= cursor->cursor->y > window->pending.y + window->pending.height / 2
                ? WLR_EDGE_BOTTOM
                : WLR_EDGE_TOP;

            const char *image = NULL;
            if (edge == (WLR_EDGE_LEFT | WLR_EDGE_TOP)) {
                image = "nw-resize";
            } else if (edge == (WLR_EDGE_TOP | WLR_EDGE_RIGHT)) {
                image = "ne-resize";
            } else if (edge == (WLR_EDGE_RIGHT | WLR_EDGE_BOTTOM)) {
                image = "se-resize";
            } else if (edge == (WLR_EDGE_BOTTOM | WLR_EDGE_LEFT)) {
                image = "sw-resize";
            }
            cursor_set_image(seat->cursor, image, NULL);

            root_set_focused_window(root, window);

            seatop_begin_resize_tiling(
                seat, window, edge
            ); // TODO (hayward) should only accept windows.

            root_commit_focus(root);
            return;
        }
    }

    // Handle beginning floating move
    if (window && is_floating && !is_fullscreen && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        uint32_t btn_move = config->floating_mod_inverse ? BTN_RIGHT : BTN_LEFT;
        if (button == btn_move && (mod_pressed || on_titlebar)) {
            root_set_focused_window(root, window);
            seatop_begin_move(seat, window); // TODO (hayward) should only accept windows.
            root_commit_focus(root);
            return;
        }
    }

    // Handle beginning floating resize
    if (window && is_floating && !is_fullscreen && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // Via border
        if (button == BTN_LEFT && resize_edge != WLR_EDGE_NONE) {
            seatop_begin_resize_floating(seat, window, resize_edge);
            root_commit_focus(root);
            return;
        }

        // Via mod+click
        uint32_t btn_resize = config->floating_mod_inverse ? BTN_LEFT : BTN_RIGHT;
        if (mod_pressed && button == btn_resize) {
            edge = 0;
            edge |= cursor->cursor->x > window->pending.x + window->pending.width / 2
                ? WLR_EDGE_RIGHT
                : WLR_EDGE_LEFT;
            edge |= cursor->cursor->y > window->pending.y + window->pending.height / 2
                ? WLR_EDGE_BOTTOM
                : WLR_EDGE_TOP;
            seatop_begin_resize_floating(seat, window, edge);
            root_commit_focus(root);
            return;
        }
    }

    // Handle moving a tiling window.
    if ((mod_pressed || on_titlebar) && state == WL_POINTER_BUTTON_STATE_PRESSED && !is_floating &&
        window && !window_is_fullscreen(window)) {

        seatop_begin_move(seat, window);
        root_commit_focus(root);
        return;
    }

    // Handle mousedown on a window surface.
    if (surface && window && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        root_set_focused_window(root, window);
        root_commit_focus(root);

        seatop_begin_down(seat, window, time_msec, sx, sy);
        root_commit_focus(root);

        seat_pointer_notify_button(seat, time_msec, button, WL_POINTER_BUTTON_STATE_PRESSED);
        return;
    }

    // Handle clicking a window surface or decorations.
    if (window && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        root_set_focused_window(root, window);
        root_commit_focus(root);

        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

#if HAVE_XWAYLAND

    // Handle clicking on xwayland unmanaged view
    if (surface != NULL) {
        struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
        if (xsurface != NULL && xsurface->override_redirect &&
            wlr_xwayland_or_surface_wants_focus(xsurface)) {
            struct wlr_xwayland *xwayland = server.xwayland->xwayland;
            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);

            root_set_focused_surface(root, xsurface->surface);
            root_commit_focus(root);

            seat_pointer_notify_button(seat, time_msec, button, state);
            return;
        }
    }
#endif

    seat_pointer_notify_button(seat, time_msec, button, state);
}

/*------------------------------------------\
 * Functions used by handle_pointer_motion  /
 *----------------------------------------*/

static void
check_focus_follows_mouse(
    struct hwd_seat *seat, struct seatop_default_event *e, struct hwd_output *output,
    struct hwd_window *window
) {
    struct hwd_window *focus = root_get_focused_window(root);

    // This is the case if a layer-shell surface is hovered.
    // If it's on another output, focus the active workspace there.
    if (output == NULL && window == NULL) {
        struct wlr_output *wlr_output = wlr_output_layout_output_at(
            root->output_layout, seat->cursor->cursor->x, seat->cursor->cursor->y
        );
        if (wlr_output == NULL) {
            return;
        }
        struct hwd_output *hovered_output = wlr_output->data;
        if (focus && hovered_output != root_get_active_output(root)) {
            root_set_active_output(root, hovered_output);
            root_commit_focus(root);
        }
        return;
    }

    // If a workspace node is hovered (eg. in the gap area), only set focus if
    // the workspace is on a different output to the previous focus.
    if (focus && output != NULL && window == NULL) {
        struct wlr_output *wlr_output = wlr_output_layout_output_at(
            root->output_layout, seat->cursor->cursor->x, seat->cursor->cursor->y
        );
        if (wlr_output == NULL) {
            return;
        }
        struct hwd_output *hovered_output = wlr_output->data;

        struct hwd_output *focused_output = root_get_active_output(root);

        if (hovered_output != focused_output) {
            root_set_active_output(root, hovered_output);
            root_commit_focus(root);
        }
        return;
    }

    // This is where we handle the common case. We don't want to focus inactive
    // tabs, hence the view_is_visible check.
    if (window != NULL && view_is_visible(window->view)) {
        // e->previous_node is the node which the cursor was over previously.
        // If focus_follows_mouse is yes and the cursor got over the view due
        // to, say, a workspace switch, we don't want to set the focus.
        // But if focus_follows_mouse is "always", we do.
        if (window != e->previous_window || config->focus_follows_mouse == FOLLOWS_ALWAYS) {
            root_set_focused_window(root, window);
            root_commit_focus(root);
        }
    }
}

static void
handle_pointer_motion(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct hwd_cursor *cursor = seat->cursor;

    struct hwd_output *output;
    struct hwd_window *window;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface, &sx, &sy
    );

    if (config->focus_follows_mouse != FOLLOWS_NO) {
        check_focus_follows_mouse(seat, e, output, window);
    }

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_seat_pointer_notify_enter(seat->wlr_seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
        }
    } else {
        cursor_update_image(cursor, window);
        wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
    }

    struct hwd_drag_icon *drag_icon;
    wl_list_for_each(drag_icon, &root->drag_icons, link) {
        if (drag_icon->seat == seat) {
            drag_icon_update_position(drag_icon);
        }
    }

    e->previous_window = window;
}

static void
handle_tablet_tool_motion(struct hwd_seat *seat, struct hwd_tablet_tool *tool, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct hwd_cursor *cursor = seat->cursor;
    struct hwd_output *output = NULL;
    struct hwd_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface, &sx, &sy
    );

    if (config->focus_follows_mouse != FOLLOWS_NO) {
        check_focus_follows_mouse(seat, e, output, window);
    }

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_tablet_v2_tablet_tool_notify_proximity_in(
                tool->tablet_v2_tool, tool->tablet->tablet_v2, surface
            );
            wlr_tablet_v2_tablet_tool_notify_motion(tool->tablet_v2_tool, sx, sy);
        }
    } else {
        cursor_update_image(cursor, window);
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->tablet_v2_tool);
    }

    struct hwd_drag_icon *drag_icon;
    wl_list_for_each(drag_icon, &root->drag_icons, link) {
        if (drag_icon->seat == seat) {
            drag_icon_update_position(drag_icon);
        }
    }

    e->previous_window = window;
}

/*----------------------------------------\
 * Functions used by handle_pointer_axis  /
 *--------------------------------------*/

static uint32_t
wl_axis_to_button(struct wlr_pointer_axis_event *event) {
    switch (event->orientation) {
    case WL_POINTER_AXIS_VERTICAL_SCROLL:
        return event->delta < 0 ? HWD_SCROLL_UP : HWD_SCROLL_DOWN;
    case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
        return event->delta < 0 ? HWD_SCROLL_LEFT : HWD_SCROLL_RIGHT;
    default:
        wlr_log(WLR_DEBUG, "Unknown axis orientation");
        return 0;
    }
}

static void
handle_pointer_axis(struct hwd_seat *seat, struct wlr_pointer_axis_event *event) {
    struct hwd_input_device *input_device = event->pointer ? event->pointer->base.data : NULL;
    struct input_config *ic = input_device ? input_device_get_config(input_device) : NULL;
    struct hwd_cursor *cursor = seat->cursor;
    struct seatop_default_event *e = seat->seatop_data;

    // Determine what's under the cursor
    struct hwd_output *output = NULL;
    struct hwd_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface, &sx, &sy
    );
    enum wlr_edges edge = window ? find_edge(window, surface, cursor) : WLR_EDGE_NONE;
    bool on_border = edge != WLR_EDGE_NONE;
    bool on_titlebar = window && !on_border && !surface;
    bool on_contents = window && !on_border && surface;
    bool on_workspace = output && !window && !surface;
    float scroll_factor = (ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

    bool handled = false;

    // Gather information needed for mouse bindings
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
    uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    struct wlr_input_device *device = input_device ? input_device->wlr_device : NULL;
    char *dev_id = device ? input_device_get_identifier(device) : strdup("*");
    uint32_t button = wl_axis_to_button(event);

    // Handle mouse bindings - x11 mouse buttons 4-7 - press event
    struct hwd_binding *binding = NULL;
    state_add_button(e, button);
    binding = get_active_mouse_binding(
        e, config->current_mode->mouse_bindings, modifiers, false, on_titlebar, on_border,
        on_contents, on_workspace, dev_id
    );

    if (binding) {
        seat_execute_command(seat, binding);
        handled = true;
    }

    // Handle mouse bindings - x11 mouse buttons 4-7 - release event
    binding = get_active_mouse_binding(
        e, config->current_mode->mouse_bindings, modifiers, true, on_titlebar, on_border,
        on_contents, on_workspace, dev_id
    );
    state_erase_button(e, button);
    if (binding) {
        seat_execute_command(seat, binding);
        handled = true;
    }
    free(dev_id);

    if (!handled) {
        wlr_seat_pointer_notify_axis(
            cursor->seat->wlr_seat, event->time_msec, event->orientation,
            scroll_factor * event->delta, round(scroll_factor * event->delta_discrete),
            event->source, event->relative_direction
        );
    }
}

/*----------------------------------\
 * Functions used by handle_rebase  /
 *--------------------------------*/

static void
handle_rebase(struct hwd_seat *seat, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct hwd_cursor *cursor = seat->cursor;
    struct hwd_output *output = NULL;
    struct hwd_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx = 0.0, sy = 0.0;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface, &sx, &sy
    );

    e->previous_window = NULL;
    if (window != NULL) {
        e->previous_window = window;
    }

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_seat_pointer_notify_enter(seat->wlr_seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(seat->wlr_seat, time_msec, sx, sy);
        }
    } else {
        cursor_update_image(cursor, e->previous_window);
        wlr_seat_pointer_notify_clear_focus(seat->wlr_seat);
    }
}

static const struct hwd_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .pointer_axis = handle_pointer_axis,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .tablet_tool_motion = handle_tablet_tool_motion,
    .rebase = handle_rebase,
    .allow_set_cursor = true,
};

void
seatop_begin_default(struct hwd_seat *seat) {
    seatop_end(seat);

    struct seatop_default_event *e = calloc(1, sizeof(struct seatop_default_event));
    assert(e);
    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    seatop_rebase(seat, 0);
}
