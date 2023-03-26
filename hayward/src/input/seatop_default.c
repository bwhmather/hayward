#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/input/seat.h"

#include <float.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
#include <wlr/xwayland.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/desktop/transaction.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/tablet.h>
#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/node.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>
#if HAVE_XWAYLAND
#include <hayward/xwayland.h>
#endif

struct seatop_default_event {
    struct hayward_window *previous_window;
    uint32_t pressed_buttons[HAYWARD_CURSOR_PRESSED_BUTTONS_CAP];
    size_t pressed_button_count;
};

/*-----------------------------------------\
 * Functions shared by multiple callbacks  /
 *---------------------------------------*/

static bool
column_edge_is_external(struct hayward_column *column, enum wlr_edges edge) {
    if (edge == WLR_EDGE_TOP) {
        return true;
    }

    if (edge == WLR_EDGE_BOTTOM) {
        return true;
    }

    hayward_assert(
        column->pending.workspace, "Column is not attached to a workspace"
    );
    list_t *columns = column->pending.workspace->pending.tiling;
    int index = list_find(columns, column);
    hayward_assert(index >= 0, "Column not found");

    if (edge == WLR_EDGE_LEFT && index == 0) {
        return true;
    }

    if (edge == WLR_EDGE_RIGHT && index == columns->length - 1) {
        return true;
    }

    return false;
}

static bool
window_edge_is_external(struct hayward_window *window, enum wlr_edges edge) {
    hayward_assert(window_is_tiling(window), "Expected tiling window");

    if (edge == WLR_EDGE_LEFT || edge == WLR_EDGE_RIGHT) {
        return column_edge_is_external(window->pending.parent, edge);
    }

    enum hayward_column_layout layout = window->pending.parent->pending.layout;

    if (layout == L_STACKED) {
        return true;
    }

    list_t *siblings = window_get_siblings(window);
    int index = list_find(siblings, window);
    hayward_assert(index >= 0, "Window not found");

    if (edge == WLR_EDGE_TOP && index == 0) {
        return true;
    }

    if (edge == WLR_EDGE_BOTTOM && index == siblings->length - 1) {
        return true;
    }

    return false;
}

static enum wlr_edges
find_edge(
    struct hayward_window *cont, struct wlr_surface *surface,
    struct hayward_cursor *cursor
) {
    if (!cont->view || (surface && cont->view->surface != surface)) {
        return WLR_EDGE_NONE;
    }
    if (cont->pending.border == B_NONE || !cont->pending.border_thickness ||
        cont->pending.border == B_CSD) {
        return WLR_EDGE_NONE;
    }
    if (cont->pending.fullscreen) {
        return WLR_EDGE_NONE;
    }

    enum wlr_edges edge = 0;
    if (cursor->cursor->x < cont->pending.x + cont->pending.border_thickness) {
        edge |= WLR_EDGE_LEFT;
    }
    if (cursor->cursor->y < cont->pending.y + cont->pending.border_thickness) {
        edge |= WLR_EDGE_TOP;
    }
    if (cursor->cursor->x >= cont->pending.x + cont->pending.width -
            cont->pending.border_thickness) {
        edge |= WLR_EDGE_RIGHT;
    }
    if (cursor->cursor->y >= cont->pending.y + cont->pending.height -
            cont->pending.border_thickness) {
        edge |= WLR_EDGE_BOTTOM;
    }

    return edge;
}

/**
 * If the cursor is over a _resizable_ edge, return the edge.
 * Edges that can't be resized are edges of the workspace.
 */
enum wlr_edges
find_resize_edge(
    struct hayward_window *cont, struct wlr_surface *surface,
    struct hayward_cursor *cursor
) {
    enum wlr_edges edge = find_edge(cont, surface, cursor);
    if (edge && (!window_is_floating(cont)) &&
        window_edge_is_external(cont, edge)) {
        return WLR_EDGE_NONE;
    }
    return edge;
}

/**
 * Return the mouse binding which matches modifier, click location, release,
 * and pressed button state, otherwise return null.
 */
static struct hayward_binding *
get_active_mouse_binding(
    struct seatop_default_event *e, list_t *bindings, uint32_t modifiers,
    bool release, bool on_titlebar, bool on_border, bool on_content,
    bool on_workspace, const char *identifier
) {
    uint32_t click_region =
        ((on_titlebar || on_workspace) ? BINDING_TITLEBAR : 0) |
        ((on_border || on_workspace) ? BINDING_BORDER : 0) |
        ((on_content || on_workspace) ? BINDING_CONTENTS : 0);

    struct hayward_binding *current = NULL;
    for (int i = 0; i < bindings->length; ++i) {
        struct hayward_binding *binding = bindings->items[i];
        if (modifiers ^ binding->modifiers ||
            e->pressed_button_count != (size_t)binding->keys->length ||
            release != (binding->flags & BINDING_RELEASE) ||
            !(click_region & binding->flags) ||
            (on_workspace && (click_region & binding->flags) != click_region) ||
            (strcmp(binding->input, identifier) != 0 &&
             strcmp(binding->input, "*") != 0)) {
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
    if (e->pressed_button_count >= HAYWARD_CURSOR_PRESSED_BUTTONS_CAP) {
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
    struct hayward_seat *seat, struct hayward_tablet_tool *tool,
    uint32_t time_msec, enum wlr_tablet_tool_tip_state state
) {
    if (state == WLR_TABLET_TOOL_TIP_UP) {
        wlr_tablet_v2_tablet_tool_notify_up(tool->tablet_v2_tool);
        return;
    }

    struct hayward_cursor *cursor = seat->cursor;
    struct hayward_output *output = NULL;
    struct hayward_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx, sy;

    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface,
        &sx, &sy
    );

    hayward_assert(
        surface,
        "Expected null-surface tablet input to route through pointer emulation"
    );

    if (wlr_surface_is_layer_surface(surface)) {
        // Handle tapping a layer surface.
        struct wlr_layer_surface_v1 *layer =
            wlr_layer_surface_v1_from_wlr_surface(surface);
        if (layer->current.keyboard_interactive) {
            root_set_focused_layer(layer);
            transaction_commit_dirty();
        }
    } else if (window) {
        bool is_floating_or_child = window_is_floating(window);
        bool is_fullscreen_or_child = window_is_fullscreen(window);
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
        bool mod_pressed = keyboard &&
            (wlr_keyboard_get_modifiers(keyboard) & config->floating_mod);

        // Handle beginning floating move.
        if (is_floating_or_child && !is_fullscreen_or_child && mod_pressed) {
            root_set_focused_window(window);
            seatop_begin_move_floating(seat, window);
            return;
        }

        // Handle moving a tiled window.
        if (config->tiling_drag && mod_pressed && !is_floating_or_child &&
            !window->pending.fullscreen) {
            seatop_begin_move_tiling(seat, window);
            return;
        }

        // Handle tapping on a container surface
        root_set_focused_window(window);
        seatop_begin_down(seat, window, time_msec, sx, sy);
    }
#if HAVE_XWAYLAND
    // Handle tapping on an xwayland unmanaged view
    else if (wlr_surface_is_xwayland_surface(surface)) {
        struct wlr_xwayland_surface *xsurface =
            wlr_xwayland_surface_from_wlr_surface(surface);
        if (xsurface->override_redirect &&
            wlr_xwayland_or_surface_wants_focus(xsurface)) {
            struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
            root_set_focused_surface(xsurface->surface);
            transaction_commit_dirty();
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
    struct hayward_seat *seat, struct wlr_input_device *device, uint32_t button,
    enum wlr_button_state state, uint32_t modifiers, bool on_titlebar,
    bool on_border, bool on_contents, bool on_workspace
) {
    // We can reach this for non-pointer devices if we're currently emulating
    // pointer input for one. Emulated input should not trigger bindings. The
    // device can be NULL if this is synthetic (e.g. haywardmsg-generated)
    // input.
    if (device && device->type != WLR_INPUT_DEVICE_POINTER) {
        return false;
    }

    struct seatop_default_event *e = seat->seatop_data;

    char *device_identifier =
        device ? input_device_get_identifier(device) : strdup("*");
    struct hayward_binding *binding = NULL;
    if (state == WLR_BUTTON_PRESSED) {
        state_add_button(e, button);
        binding = get_active_mouse_binding(
            e, config->current_mode->mouse_bindings, modifiers, false,
            on_titlebar, on_border, on_contents, on_workspace, device_identifier
        );
    } else {
        binding = get_active_mouse_binding(
            e, config->current_mode->mouse_bindings, modifiers, true,
            on_titlebar, on_border, on_contents, on_workspace, device_identifier
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
    struct hayward_seat *seat, uint32_t time_msec,
    struct wlr_input_device *device, uint32_t button,
    enum wlr_button_state state
) {
    struct hayward_cursor *cursor = seat->cursor;

    // Determine what's under the cursor.
    struct hayward_output *output;
    struct hayward_window *window;
    struct wlr_surface *surface = NULL;
    double sx, sy;

    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface,
        &sx, &sy
    );

    struct hayward_workspace *workspace = root_get_active_workspace();

    bool is_floating = window && window_is_floating(window);
    bool is_fullscreen = window && window_is_fullscreen(window);
    enum wlr_edges edge =
        window ? find_edge(window, surface, cursor) : WLR_EDGE_NONE;
    enum wlr_edges resize_edge = window && edge
        ? find_resize_edge(window, surface, cursor)
        : WLR_EDGE_NONE;
    bool on_border = edge != WLR_EDGE_NONE;
    bool on_contents = window && !on_border && surface;
    bool on_workspace = output && !window && !surface;
    bool on_titlebar = window && !on_border && !surface;

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
    uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;

    // Handle mouse bindings
    if (trigger_pointer_button_binding(
            seat, device, button, state, modifiers, on_titlebar, on_border,
            on_contents, on_workspace
        )) {
        return;
    }

    // Handle clicking an empty workspace
    if (output && !window && !surface) {
        if (state == WLR_BUTTON_PRESSED) {
            workspace_set_active_window(workspace, NULL);
            transaction_commit_dirty();
        }
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

    // Handle clicking a layer surface
    if (surface && wlr_surface_is_layer_surface(surface)) {
        struct wlr_layer_surface_v1 *layer =
            wlr_layer_surface_v1_from_wlr_surface(surface);
        if (layer->current.keyboard_interactive) {
            root_set_focused_layer(layer);
            transaction_commit_dirty();
        }
        if (state == WLR_BUTTON_PRESSED) {
            seatop_begin_down_on_surface(seat, surface, time_msec, sx, sy);
        }
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

    // Handle tiling resize via border
    if (window && resize_edge && button == BTN_LEFT &&
        state == WLR_BUTTON_PRESSED && !is_floating) {
        // If a resize is triggered on a window within a stacked
        // column, change focus to the tab which already had inactive
        // focus -- otherwise, if the user clicked on the title of a
        // hidden tab, we'd change the active tab when the user
        // probably just wanted to resize.
        struct hayward_window *window_to_focus = window;
        struct hayward_column *parent = window->pending.parent;
        if (parent->pending.layout == L_STACKED) {
            window_to_focus = parent->pending.active_child;
        }
        root_set_focused_window(window_to_focus);
        seatop_begin_resize_tiling(
            seat, window, edge
        ); // TODO (hayward) will only ever take a window.
        return;
    }

    // Handle tiling resize via mod
    bool mod_pressed = modifiers & config->floating_mod;
    if (window && !is_floating && mod_pressed && state == WLR_BUTTON_PRESSED) {
        uint32_t btn_resize =
            config->floating_mod_inverse ? BTN_LEFT : BTN_RIGHT;
        if (button == btn_resize) {
            edge = 0;
            edge |= cursor->cursor->x >
                    window->pending.x + window->pending.width / 2
                ? WLR_EDGE_RIGHT
                : WLR_EDGE_LEFT;
            edge |= cursor->cursor->y >
                    window->pending.y + window->pending.height / 2
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
            root_set_focused_window(window);
            seatop_begin_resize_tiling(
                seat, window, edge
            ); // TODO (hayward) should only accept windows.
            return;
        }
    }

    // Handle beginning floating move
    if (window && is_floating && !is_fullscreen &&
        state == WLR_BUTTON_PRESSED) {
        uint32_t btn_move = config->floating_mod_inverse ? BTN_RIGHT : BTN_LEFT;
        if (button == btn_move && (mod_pressed || on_titlebar)) {
            root_set_focused_window(window);
            seatop_begin_move_floating(
                seat, window
            ); // TODO (hayward) should only accept windows.
            return;
        }
    }

    // Handle beginning floating resize
    if (window && is_floating && !is_fullscreen &&
        state == WLR_BUTTON_PRESSED) {
        // Via border
        if (button == BTN_LEFT && resize_edge != WLR_EDGE_NONE) {
            seatop_begin_resize_floating(seat, window, resize_edge);
            return;
        }

        // Via mod+click
        uint32_t btn_resize =
            config->floating_mod_inverse ? BTN_LEFT : BTN_RIGHT;
        if (mod_pressed && button == btn_resize) {
            edge = 0;
            edge |= cursor->cursor->x >
                    window->pending.x + window->pending.width / 2
                ? WLR_EDGE_RIGHT
                : WLR_EDGE_LEFT;
            edge |= cursor->cursor->y >
                    window->pending.y + window->pending.height / 2
                ? WLR_EDGE_BOTTOM
                : WLR_EDGE_TOP;
            seatop_begin_resize_floating(seat, window, edge);
            return;
        }
    }

    // Handle moving a tiling container
    if (config->tiling_drag && (mod_pressed || on_titlebar) &&
        state == WLR_BUTTON_PRESSED && !is_floating && window &&
        !window->pending.fullscreen) {
        struct hayward_window *focus = root_get_focused_window();
        if (on_titlebar && focus != window) {
            root_set_focused_window(window);
        }

        // If moving a container by its title bar, use a threshold for the drag
        if (!mod_pressed && config->tiling_drag_threshold > 0) {
            seatop_begin_move_tiling_threshold(seat, window);
        } else {
            seatop_begin_move_tiling(seat, window);
        }
        return;
    }

    // Handle mousedown on a container surface
    if (surface && window && state == WLR_BUTTON_PRESSED) {
        root_set_focused_window(window);
        seatop_begin_down(seat, window, time_msec, sx, sy);
        seat_pointer_notify_button(seat, time_msec, button, WLR_BUTTON_PRESSED);
        return;
    }

    // Handle clicking a container surface or decorations
    if (window && state == WLR_BUTTON_PRESSED) {
        root_set_focused_window(window);
        transaction_commit_dirty();
        seat_pointer_notify_button(seat, time_msec, button, state);
        return;
    }

#if HAVE_XWAYLAND
    // Handle clicking on xwayland unmanaged view
    if (surface && wlr_surface_is_xwayland_surface(surface)) {
        struct wlr_xwayland_surface *xsurface =
            wlr_xwayland_surface_from_wlr_surface(surface);
        if (xsurface->override_redirect &&
            wlr_xwayland_or_surface_wants_focus(xsurface)) {
            struct wlr_xwayland *xwayland = server.xwayland.wlr_xwayland;
            wlr_xwayland_set_seat(xwayland, seat->wlr_seat);
            root_set_focused_surface(xsurface->surface);
            transaction_commit_dirty();
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
    struct hayward_seat *seat, struct seatop_default_event *e,
    struct hayward_output *output, struct hayward_window *window
) {
    struct hayward_window *focus = root_get_focused_window();

    // This is the case if a layer-shell surface is hovered.
    // If it's on another output, focus the active workspace there.
    if (output == NULL && window == NULL) {
        struct wlr_output *wlr_output = wlr_output_layout_output_at(
            root->output_layout, seat->cursor->cursor->x,
            seat->cursor->cursor->y
        );
        if (wlr_output == NULL) {
            return;
        }
        struct hayward_output *hovered_output = wlr_output->data;
        if (focus && hovered_output != root_get_active_output()) {
            root_set_active_output(hovered_output);
            transaction_commit_dirty();
        }
        return;
    }

    // If a workspace node is hovered (eg. in the gap area), only set focus if
    // the workspace is on a different output to the previous focus.
    if (focus && output != NULL && window == NULL) {
        struct wlr_output *wlr_output = wlr_output_layout_output_at(
            root->output_layout, seat->cursor->cursor->x,
            seat->cursor->cursor->y
        );
        if (wlr_output == NULL) {
            return;
        }
        struct hayward_output *hovered_output = wlr_output->data;

        struct hayward_output *focused_output = root_get_active_output();

        if (hovered_output != focused_output) {
            root_set_active_output(hovered_output);
            transaction_commit_dirty();
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
        if (window != e->previous_window ||
            config->focus_follows_mouse == FOLLOWS_ALWAYS) {
            root_set_focused_window(window);
            transaction_commit_dirty();
        }
    }
}

static void
handle_pointer_motion(struct hayward_seat *seat, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct hayward_cursor *cursor = seat->cursor;

    struct hayward_output *output;
    struct hayward_window *window;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface,
        &sx, &sy
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

    struct hayward_drag_icon *drag_icon;
    wl_list_for_each(drag_icon, &root->drag_icons, link) {
        if (drag_icon->seat == seat) {
            drag_icon_update_position(drag_icon);
        }
    }

    e->previous_window = window;
}

static void
handle_tablet_tool_motion(
    struct hayward_seat *seat, struct hayward_tablet_tool *tool,
    uint32_t time_msec
) {
    struct seatop_default_event *e = seat->seatop_data;
    struct hayward_cursor *cursor = seat->cursor;
    struct hayward_output *output = NULL;
    struct hayward_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface,
        &sx, &sy
    );

    if (config->focus_follows_mouse != FOLLOWS_NO) {
        check_focus_follows_mouse(seat, e, output, window);
    }

    if (surface) {
        if (seat_is_input_allowed(seat, surface)) {
            wlr_tablet_v2_tablet_tool_notify_proximity_in(
                tool->tablet_v2_tool, tool->tablet->tablet_v2, surface
            );
            wlr_tablet_v2_tablet_tool_notify_motion(
                tool->tablet_v2_tool, sx, sy
            );
        }
    } else {
        cursor_update_image(cursor, window);
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->tablet_v2_tool);
    }

    struct hayward_drag_icon *drag_icon;
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
    case WLR_AXIS_ORIENTATION_VERTICAL:
        return event->delta < 0 ? HAYWARD_SCROLL_UP : HAYWARD_SCROLL_DOWN;
    case WLR_AXIS_ORIENTATION_HORIZONTAL:
        return event->delta < 0 ? HAYWARD_SCROLL_LEFT : HAYWARD_SCROLL_RIGHT;
    default:
        hayward_log(HAYWARD_DEBUG, "Unknown axis orientation");
        return 0;
    }
}

static void
handle_pointer_axis(
    struct hayward_seat *seat, struct wlr_pointer_axis_event *event
) {
    struct hayward_input_device *input_device =
        event->pointer ? event->pointer->base.data : NULL;
    struct input_config *ic =
        input_device ? input_device_get_config(input_device) : NULL;
    struct hayward_cursor *cursor = seat->cursor;
    struct seatop_default_event *e = seat->seatop_data;

    // Determine what's under the cursor
    struct hayward_output *output = NULL;
    struct hayward_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx, sy;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface,
        &sx, &sy
    );
    enum wlr_edges edge =
        window ? find_edge(window, surface, cursor) : WLR_EDGE_NONE;
    bool on_border = edge != WLR_EDGE_NONE;
    bool on_titlebar = window && !on_border && !surface;
    bool on_titlebar_border =
        window && on_border && cursor->cursor->y < window->pending.content_y;
    bool on_contents = window && !on_border && surface;
    bool on_workspace = output && !window && !surface;
    float scroll_factor =
        (ic == NULL || ic->scroll_factor == FLT_MIN) ? 1.0f : ic->scroll_factor;

    bool handled = false;

    // Gather information needed for mouse bindings
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->wlr_seat);
    uint32_t modifiers = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    struct wlr_input_device *device =
        input_device ? input_device->wlr_device : NULL;
    char *dev_id = device ? input_device_get_identifier(device) : strdup("*");
    uint32_t button = wl_axis_to_button(event);

    // Handle mouse bindings - x11 mouse buttons 4-7 - press event
    struct hayward_binding *binding = NULL;
    state_add_button(e, button);
    binding = get_active_mouse_binding(
        e, config->current_mode->mouse_bindings, modifiers, false, on_titlebar,
        on_border, on_contents, on_workspace, dev_id
    );
    if (binding) {
        seat_execute_command(seat, binding);
        handled = true;
    }

    // Scrolling on a stacked title bar (handled as press event)
    if (!handled && (on_titlebar || on_titlebar_border)) {
        struct hayward_column *column = window->pending.parent;
        if (column->pending.layout == L_STACKED) {
            struct hayward_window *active = column->pending.active_child;
            list_t *siblings = window_get_siblings(window);
            int desired = list_find(siblings, active) +
                round(scroll_factor * event->delta_discrete);
            if (desired < 0) {
                desired = 0;
            } else if (desired >= siblings->length) {
                desired = siblings->length - 1;
            }

            struct hayward_window *new_sibling = siblings->items[desired];
            root_set_focused_window(new_sibling);
            transaction_commit_dirty();
            handled = true;
        }
    }

    // Handle mouse bindings - x11 mouse buttons 4-7 - release event
    binding = get_active_mouse_binding(
        e, config->current_mode->mouse_bindings, modifiers, true, on_titlebar,
        on_border, on_contents, on_workspace, dev_id
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
            scroll_factor * event->delta,
            round(scroll_factor * event->delta_discrete), event->source
        );
    }
}

/*----------------------------------\
 * Functions used by handle_rebase  /
 *--------------------------------*/

static void
handle_rebase(struct hayward_seat *seat, uint32_t time_msec) {
    struct seatop_default_event *e = seat->seatop_data;
    struct hayward_cursor *cursor = seat->cursor;
    struct hayward_output *output = NULL;
    struct hayward_window *window = NULL;
    struct wlr_surface *surface = NULL;
    double sx = 0.0, sy = 0.0;
    seat_get_target_at(
        seat, cursor->cursor->x, cursor->cursor->y, &output, &window, &surface,
        &sx, &sy
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

static const struct hayward_seatop_impl seatop_impl = {
    .button = handle_button,
    .pointer_motion = handle_pointer_motion,
    .pointer_axis = handle_pointer_axis,
    .tablet_tool_tip = handle_tablet_tool_tip,
    .tablet_tool_motion = handle_tablet_tool_motion,
    .rebase = handle_rebase,
    .allow_set_cursor = true,
};

void
seatop_begin_default(struct hayward_seat *seat) {
    seatop_end(seat);

    struct seatop_default_event *e =
        calloc(1, sizeof(struct seatop_default_event));
    hayward_assert(e, "Unable to allocate seatop_default_event");
    seat->seatop_impl = &seatop_impl;
    seat->seatop_data = e;

    seatop_rebase(seat, 0);
}
