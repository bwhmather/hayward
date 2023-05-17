#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/ipc-json.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <json.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/box.h>
#include <wlr/xwayland/xwayland.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <wayland-server-protocol.h>

#include <hayward/config.h>
#include <hayward/desktop/idle_inhibit_v1.h>
#include <hayward/globals/root.h>
#include <hayward/input/cursor.h>
#include <hayward/input/input-manager.h>
#include <hayward/input/seat.h>
#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>
#include <hayward/xwayland.h>

#include <config.h>

static const char *
ipc_json_layout_description(enum hayward_column_layout l) {
    switch (l) {
    case L_SPLIT:
        return "split";
    case L_STACKED:
        return "stacked";
    }
    hayward_abort("invalid layout");
}

static const char *
ipc_json_border_description(enum hayward_window_border border) {
    switch (border) {
    case B_NONE:
        return "none";
    case B_PIXEL:
        return "pixel";
    case B_NORMAL:
        return "normal";
    case B_CSD:
        return "csd";
    }
    return "unknown";
}

static const char *
ipc_json_output_transform_description(enum wl_output_transform transform) {
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
        return "normal";
    case WL_OUTPUT_TRANSFORM_90:
        // Hayward uses clockwise transforms, while WL_OUTPUT_TRANSFORM_*
        // describes anti-clockwise transforms.
        return "270";
    case WL_OUTPUT_TRANSFORM_180:
        return "180";
    case WL_OUTPUT_TRANSFORM_270:
        // Transform also inverted here.
        return "90";
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        return "flipped";
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        // Inverted.
        return "flipped-270";
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        return "flipped-180";
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        // Inverted.
        return "flipped-90";
    }
    return NULL;
}

static const char *
ipc_json_output_adaptive_sync_status_description(
    enum wlr_output_adaptive_sync_status status
) {
    switch (status) {
    case WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED:
        return "disabled";
    case WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED:
        return "enabled";
    }
    return NULL;
}

#if HAVE_XWAYLAND
static const char *
ipc_json_xwindow_type_description(struct hayward_view *view) {
    struct wlr_xwayland_surface *surface = view->wlr_xwayland_surface;
    struct hayward_xwayland *xwayland = &server.xwayland;

    for (size_t i = 0; i < surface->window_type_len; ++i) {
        xcb_atom_t type = surface->window_type[i];
        if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_NORMAL]) {
            return "normal";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_DIALOG]) {
            return "dialog";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_UTILITY]) {
            return "utility";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_TOOLBAR]) {
            return "toolbar";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_SPLASH]) {
            return "splash";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_MENU]) {
            return "menu";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_DROPDOWN_MENU]) {
            return "dropdown_menu";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_POPUP_MENU]) {
            return "popup_menu";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_TOOLTIP]) {
            return "tooltip";
        } else if (type == xwayland->atoms[NET_WM_WINDOW_TYPE_NOTIFICATION]) {
            return "notification";
        } else {
            return "unknown";
        }
    }

    return "unknown";
}
#endif

static const char *
ipc_json_user_idle_inhibitor_description(enum hayward_idle_inhibit_mode mode) {
    switch (mode) {
    case INHIBIT_IDLE_FOCUS:
        return "focus";
    case INHIBIT_IDLE_FULLSCREEN:
        return "fullscreen";
    case INHIBIT_IDLE_OPEN:
        return "open";
    case INHIBIT_IDLE_VISIBLE:
        return "visible";
    case INHIBIT_IDLE_APPLICATION:
        return NULL;
    }
    return NULL;
}

json_object *
ipc_json_get_version(void) {
    int major = 0, minor = 0, patch = 0;
    json_object *version = json_object_new_object();

    sscanf(HAYWARD_VERSION, "%d.%d.%d", &major, &minor, &patch);

    json_object_object_add(
        version, "human_readable", json_object_new_string(HAYWARD_VERSION)
    );
    json_object_object_add(
        version, "variant", json_object_new_string("hayward")
    );
    json_object_object_add(version, "major", json_object_new_int(major));
    json_object_object_add(version, "minor", json_object_new_int(minor));
    json_object_object_add(version, "patch", json_object_new_int(patch));
    json_object_object_add(
        version, "loaded_config_file_name",
        json_object_new_string(config->current_config_path)
    );

    return version;
}

json_object *
ipc_json_get_binding_mode(void) {
    json_object *current_mode = json_object_new_object();
    json_object_object_add(
        current_mode, "name", json_object_new_string(config->current_mode->name)
    );
    return current_mode;
}

static json_object *
ipc_json_create_rect(struct wlr_box *box) {
    json_object *rect = json_object_new_object();

    json_object_object_add(rect, "x", json_object_new_int(box->x));
    json_object_object_add(rect, "y", json_object_new_int(box->y));
    json_object_object_add(rect, "width", json_object_new_int(box->width));
    json_object_object_add(rect, "height", json_object_new_int(box->height));

    return rect;
}

static json_object *
ipc_json_create_empty_rect(void) {
    struct wlr_box empty = {0, 0, 0, 0};

    return ipc_json_create_rect(&empty);
}

static json_object *
ipc_json_create_node(
    int id, const char *type, char *name, struct wlr_box *box
) {
    json_object *object = json_object_new_object();

    json_object_object_add(object, "id", json_object_new_int(id));
    json_object_object_add(object, "type", json_object_new_string(type));
    json_object_object_add(object, "percent", NULL);
    json_object_object_add(object, "urgent", json_object_new_boolean(false));

    // set default values to be compatible with i3
    json_object_object_add(
        object, "border",
        json_object_new_string(ipc_json_border_description(B_NONE))
    );
    json_object_object_add(
        object, "current_border_width", json_object_new_int(0)
    );
    json_object_object_add(object, "rect", ipc_json_create_rect(box));
    json_object_object_add(object, "deco_rect", ipc_json_create_empty_rect());
    json_object_object_add(object, "window_rect", ipc_json_create_empty_rect());
    json_object_object_add(object, "geometry", ipc_json_create_empty_rect());
    json_object_object_add(
        object, "name", name ? json_object_new_string(name) : NULL
    );
    json_object_object_add(object, "window", NULL);
    json_object_object_add(object, "nodes", json_object_new_array());
    json_object_object_add(object, "floating_nodes", json_object_new_array());
    json_object_object_add(object, "fullscreen_mode", json_object_new_int(0));

    return object;
}

json_object *
ipc_json_describe_output(struct hayward_output *output) {
    char *name = output->wlr_output->name;

    struct wlr_box box;
    output_get_box(output, &box);

    json_object *object =
        ipc_json_create_node(output->id, "output", name, &box);

    struct wlr_output *wlr_output = output->wlr_output;
    json_object_object_add(object, "active", json_object_new_boolean(true));
    json_object_object_add(
        object, "dpms", json_object_new_boolean(wlr_output->enabled)
    );
    bool focused = root_get_active_output(root) == output;
    json_object_object_add(object, "focused", json_object_new_boolean(focused));
    json_object_object_add(object, "primary", json_object_new_boolean(false));
    json_object_object_add(object, "layout", json_object_new_string("output"));
    json_object_object_add(
        object, "make", json_object_new_string(wlr_output->make)
    );
    json_object_object_add(
        object, "model", json_object_new_string(wlr_output->model)
    );
    json_object_object_add(
        object, "serial", json_object_new_string(wlr_output->serial)
    );
    json_object_object_add(
        object, "scale", json_object_new_double(wlr_output->scale)
    );
    json_object_object_add(
        object, "scale_filter",
        json_object_new_string(
            hayward_output_scale_filter_to_string(output->scale_filter)
        )
    );
    json_object_object_add(
        object, "transform",
        json_object_new_string(
            ipc_json_output_transform_description(wlr_output->transform)
        )
    );
    const char *adaptive_sync_status =
        ipc_json_output_adaptive_sync_status_description(
            wlr_output->adaptive_sync_status
        );
    json_object_object_add(
        object, "adaptive_sync_status",
        json_object_new_string(adaptive_sync_status)
    );

    struct hayward_workspace *workspace = root_get_active_workspace(root);
    hayward_assert(workspace, "Expected output to have a workspace");

    json_object_object_add(
        object, "current_workspace", json_object_new_string(workspace->name)
    );

    json_object *modes_array = json_object_new_array();
    struct wlr_output_mode *mode;
    wl_list_for_each(mode, &wlr_output->modes, link) {
        json_object *mode_object = json_object_new_object();
        json_object_object_add(
            mode_object, "width", json_object_new_int(mode->width)
        );
        json_object_object_add(
            mode_object, "height", json_object_new_int(mode->height)
        );
        json_object_object_add(
            mode_object, "refresh", json_object_new_int(mode->refresh)
        );
        json_object_array_add(modes_array, mode_object);
    }

    json_object_object_add(object, "modes", modes_array);

    json_object *current_mode_object = json_object_new_object();
    json_object_object_add(
        current_mode_object, "width", json_object_new_int(wlr_output->width)
    );
    json_object_object_add(
        current_mode_object, "height", json_object_new_int(wlr_output->height)
    );
    json_object_object_add(
        current_mode_object, "refresh", json_object_new_int(wlr_output->refresh)
    );
    json_object_object_add(object, "current_mode", current_mode_object);

    json_object_object_add(
        object, "max_render_time", json_object_new_int(output->max_render_time)
    );

    return object;
}

json_object *
ipc_json_describe_disabled_output(struct hayward_output *output) {
    struct wlr_output *wlr_output = output->wlr_output;

    json_object *object = json_object_new_object();

    json_object_object_add(object, "focused", json_object_new_boolean(false));
    json_object_object_add(object, "type", json_object_new_string("output"));
    json_object_object_add(
        object, "name", json_object_new_string(wlr_output->name)
    );
    json_object_object_add(object, "active", json_object_new_boolean(false));
    json_object_object_add(object, "dpms", json_object_new_boolean(false));
    json_object_object_add(object, "primary", json_object_new_boolean(false));
    json_object_object_add(
        object, "make", json_object_new_string(wlr_output->make)
    );
    json_object_object_add(
        object, "model", json_object_new_string(wlr_output->model)
    );
    json_object_object_add(
        object, "serial", json_object_new_string(wlr_output->serial)
    );

    json_object *modes_array = json_object_new_array();
    struct wlr_output_mode *mode;
    wl_list_for_each(mode, &wlr_output->modes, link) {
        json_object *mode_object = json_object_new_object();
        json_object_object_add(
            mode_object, "width", json_object_new_int(mode->width)
        );
        json_object_object_add(
            mode_object, "height", json_object_new_int(mode->height)
        );
        json_object_object_add(
            mode_object, "refresh", json_object_new_int(mode->refresh)
        );
        json_object_array_add(modes_array, mode_object);
    }

    json_object_object_add(object, "modes", modes_array);

    json_object_object_add(object, "current_workspace", NULL);

    json_object *rect_object = json_object_new_object();
    json_object_object_add(rect_object, "x", json_object_new_int(0));
    json_object_object_add(rect_object, "y", json_object_new_int(0));
    json_object_object_add(rect_object, "width", json_object_new_int(0));
    json_object_object_add(rect_object, "height", json_object_new_int(0));
    json_object_object_add(object, "rect", rect_object);

    json_object_object_add(object, "percent", NULL);

    return object;
}

json_object *
ipc_json_describe_workspace(struct hayward_workspace *workspace) {
    char *name = workspace->name;

    struct wlr_box box;
    workspace_get_box(workspace, &box);

    json_object *object =
        ipc_json_create_node(workspace->id, "workspace", name, &box);

    int num;
    if (isdigit(workspace->name[0])) {
        errno = 0;
        char *endptr = NULL;
        long long parsed_num = strtoll(workspace->name, &endptr, 10);
        if (errno != 0 || parsed_num > INT32_MAX || parsed_num < 0 ||
            endptr == workspace->name) {
            num = -1;
        } else {
            num = (int)parsed_num;
        }
    } else {
        num = -1;
    }
    json_object_object_add(object, "num", json_object_new_int(num));
    json_object_object_add(
        object, "focused", json_object_new_boolean(workspace->pending.focused)
    );
    json_object_object_add(object, "fullscreen_mode", json_object_new_int(1));
    json_object_object_add(
        object, "urgent", json_object_new_boolean(workspace->urgent)
    );

    // Floating
    json_object *floating_array = json_object_new_array();
    for (int i = 0; i < workspace->pending.floating->length; ++i) {
        struct hayward_window *floater = workspace->pending.floating->items[i];
        json_object_array_add(
            floating_array, ipc_json_describe_window(floater)
        );
    }
    json_object_object_add(object, "floating_nodes", floating_array);

    json_object *children = json_object_new_array();
    for (int i = 0; i < workspace->pending.tiling->length; ++i) {
        struct hayward_column *column = workspace->pending.tiling->items[i];
        json_object_array_add(children, ipc_json_describe_column(column));
    }
    json_object_object_add(object, "nodes", children);

    return object;
}

static void
window_get_deco_rect(struct hayward_window *window, struct wlr_box *deco_rect) {
    if (window->current.border != B_NORMAL || window->pending.fullscreen ||
        window->pending.workspace == NULL) {
        deco_rect->x = deco_rect->y = deco_rect->width = deco_rect->height = 0;
        return;
    }

    if (window_is_floating(window)) {
        deco_rect->x = window->pending.x - window->pending.workspace->pending.x;
        deco_rect->y = window->pending.y - window->pending.workspace->pending.y;
        deco_rect->width = window->pending.width;
        deco_rect->height = window_titlebar_height();
    } else {
        struct hayward_column *parent = window->pending.parent;

        deco_rect->x = window->pending.x - parent->pending.x;
        deco_rect->y = window->pending.y - parent->pending.y;
        deco_rect->width = window->pending.width;
        deco_rect->height = window_titlebar_height();

        if (parent->pending.layout == L_STACKED) {
            if (!window->view) {
                size_t siblings = window_get_siblings(window)->length;
                deco_rect->y -= deco_rect->height * siblings;
            }
            deco_rect->y += deco_rect->height * window_sibling_index(window);
        }
    }
}

static void
ipc_json_describe_view(struct hayward_window *c, json_object *object) {
    json_object_object_add(object, "pid", json_object_new_int(c->view->pid));

    const char *app_id = view_get_app_id(c->view);
    json_object_object_add(
        object, "app_id", app_id ? json_object_new_string(app_id) : NULL
    );

    json_object_object_add(
        object, "focused", json_object_new_boolean(c->pending.focused)
    );

    bool visible = view_is_visible(c->view);
    json_object_object_add(object, "visible", json_object_new_boolean(visible));

    struct wlr_box window_box = {
        c->pending.content_x - c->pending.x,
        (c->current.border == B_PIXEL) ? c->current.border_thickness : 0,
        c->pending.content_width, c->pending.content_height};

    json_object_object_add(
        object, "window_rect", ipc_json_create_rect(&window_box)
    );

    struct wlr_box geometry = {
        0, 0, c->view->natural_width, c->view->natural_height};
    json_object_object_add(object, "geometry", ipc_json_create_rect(&geometry));

    json_object_object_add(
        object, "max_render_time", json_object_new_int(c->view->max_render_time)
    );

    json_object_object_add(
        object, "shell", json_object_new_string(view_get_shell(c->view))
    );

    json_object_object_add(
        object, "inhibit_idle",
        json_object_new_boolean(view_inhibit_idle(c->view))
    );

    json_object *idle_inhibitors = json_object_new_object();

    struct hayward_idle_inhibitor_v1 *user_inhibitor =
        hayward_idle_inhibit_v1_user_inhibitor_for_view(c->view);

    if (user_inhibitor) {
        json_object_object_add(
            idle_inhibitors, "user",
            json_object_new_string(
                ipc_json_user_idle_inhibitor_description(user_inhibitor->mode)
            )
        );
    } else {
        json_object_object_add(
            idle_inhibitors, "user", json_object_new_string("none")
        );
    }

    struct hayward_idle_inhibitor_v1 *application_inhibitor =
        hayward_idle_inhibit_v1_application_inhibitor_for_view(c->view);

    if (application_inhibitor) {
        json_object_object_add(
            idle_inhibitors, "application", json_object_new_string("enabled")
        );
    } else {
        json_object_object_add(
            idle_inhibitors, "application", json_object_new_string("none")
        );
    }

    json_object_object_add(object, "idle_inhibitors", idle_inhibitors);

#if HAVE_XWAYLAND
    if (c->view->type == HAYWARD_VIEW_XWAYLAND) {
        json_object_object_add(
            object, "window",
            json_object_new_int(view_get_x11_window_id(c->view))
        );

        json_object *window_props = json_object_new_object();

        const char *class = view_get_class(c->view);
        if (class) {
            json_object_object_add(
                window_props, "class", json_object_new_string(class)
            );
        }
        const char *instance = view_get_instance(c->view);
        if (instance) {
            json_object_object_add(
                window_props, "instance", json_object_new_string(instance)
            );
        }
        if (c->title) {
            json_object_object_add(
                window_props, "title", json_object_new_string(c->title)
            );
        }

        // the transient_for key is always present in i3's output
        uint32_t parent_id = view_get_x11_parent_id(c->view);
        json_object_object_add(
            window_props, "transient_for",
            parent_id ? json_object_new_int(parent_id) : NULL
        );

        const char *role = view_get_window_role(c->view);
        if (role) {
            json_object_object_add(
                window_props, "window_role", json_object_new_string(role)
            );
        }

        uint32_t window_type = view_get_window_type(c->view);
        if (window_type) {
            json_object_object_add(
                window_props, "window_type",
                json_object_new_string(ipc_json_xwindow_type_description(c->view
                ))
            );
        }

        json_object_object_add(object, "window_properties", window_props);
    }
#endif
}

json_object *
ipc_json_describe_column(struct hayward_column *column) {
    char *name = "column";

    struct wlr_box box;
    column_get_box(column, &box);

    json_object *object =
        ipc_json_create_node(column->id, "container", name, &box);

    json_object_object_add(
        object, "focused", json_object_new_boolean(column->pending.focused)
    );
    json_object_object_add(
        object, "layout",
        json_object_new_string(
            ipc_json_layout_description(column->pending.layout)
        )
    );

    bool urgent = column_has_urgent_child(column);
    json_object_object_add(object, "urgent", json_object_new_boolean(urgent));

    struct wlr_box parent_box = {0, 0, 0, 0};
    if (column->pending.workspace != NULL) {
        workspace_get_box(column->pending.workspace, &parent_box);
    }

    if (parent_box.width != 0 && parent_box.height != 0) {
        double percent = ((double)column->pending.width / parent_box.width) *
            ((double)column->pending.height / parent_box.height);
        json_object_object_add(
            object, "percent", json_object_new_double(percent)
        );
    }

    json_object_object_add(object, "floating_nodes", json_object_new_array());

    json_object *children = json_object_new_array();
    if (column->pending.children) {
        for (int i = 0; i < column->pending.children->length; ++i) {
            struct hayward_window *window = column->pending.children->items[i];
            json_object_array_add(children, ipc_json_describe_window(window));
        }
    }
    json_object_object_add(object, "nodes", children);

    return object;
}

json_object *
ipc_json_describe_window(struct hayward_window *window) {
    char *name = window->title;

    struct wlr_box box;
    window_get_box(window, &box);

    struct wlr_box deco_rect = {0, 0, 0, 0};
    window_get_deco_rect(window, &deco_rect);
    size_t count = 1;
    if (window->pending.parent != NULL &&
        window->pending.parent->pending.layout == L_STACKED) {
        count = window_get_siblings(window)->length;
    }
    box.y += deco_rect.height * count;
    box.height -= deco_rect.height * count;

    json_object *object =
        ipc_json_create_node(window->id, "container", name, &box);

    json_object_object_add(
        object, "focused", json_object_new_boolean(window->pending.focused)
    );
    json_object_object_add(
        object, "name",
        window->title ? json_object_new_string(window->title) : NULL
    );
    if (window_is_floating(window)) {
        json_object_object_add(
            object, "type", json_object_new_string("floating_container")
        );
    }

    bool urgent = view_is_urgent(window->view);
    json_object_object_add(object, "urgent", json_object_new_boolean(urgent));

    json_object_object_add(
        object, "fullscreen_mode",
        json_object_new_int(window->pending.fullscreen)
    );

    struct wlr_box parent_box = {0, 0, 0, 0};
    if (window->pending.parent != NULL) {
        column_get_box(window->pending.parent, &parent_box);
    } else {
        workspace_get_box(window->pending.workspace, &parent_box);
    }

    if (parent_box.width != 0 && parent_box.height != 0) {
        double percent = ((double)window->pending.width / parent_box.width) *
            ((double)window->pending.height / parent_box.height);
        json_object_object_add(
            object, "percent", json_object_new_double(percent)
        );
    }

    json_object_object_add(
        object, "border",
        json_object_new_string(
            ipc_json_border_description(window->current.border)
        )
    );
    json_object_object_add(
        object, "current_border_width",
        json_object_new_int(window->current.border_thickness)
    );
    json_object_object_add(object, "floating_nodes", json_object_new_array());

    struct wlr_box deco_box = {0, 0, 0, 0};
    window_get_deco_rect(window, &deco_box);
    json_object_object_add(
        object, "deco_rect", ipc_json_create_rect(&deco_box)
    );

    ipc_json_describe_view(window, object);

    return object;
}

json_object *
ipc_json_describe_root(struct hayward_root *root) {
    char *name = "root";

    struct wlr_box box = {0};

    json_object *object = ipc_json_create_node(1, "root", name, &box);

    json_object *children = json_object_new_array();
    for (int i = 0; i < root->outputs->length; ++i) {
        struct hayward_output *output = root->outputs->items[i];
        json_object_array_add(children, ipc_json_describe_output(output));
    }
    for (int i = 0; i < root->pending.workspaces->length; ++i) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        json_object_array_add(children, ipc_json_describe_workspace(workspace));
    }

    json_object_object_add(object, "nodes", children);

    return object;
}

static json_object *
describe_libinput_device(struct libinput_device *device) {
    json_object *object = json_object_new_object();

    const char *events = "unknown";
    switch (libinput_device_config_send_events_get_mode(device)) {
    case LIBINPUT_CONFIG_SEND_EVENTS_ENABLED:
        events = "enabled";
        break;
    case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE:
        events = "disabled_on_external_mouse";
        break;
    case LIBINPUT_CONFIG_SEND_EVENTS_DISABLED:
        events = "disabled";
        break;
    }
    json_object_object_add(
        object, "send_events", json_object_new_string(events)
    );

    if (libinput_device_config_tap_get_finger_count(device) > 0) {
        const char *tap = "unknown";
        switch (libinput_device_config_tap_get_enabled(device)) {
        case LIBINPUT_CONFIG_TAP_ENABLED:
            tap = "enabled";
            break;
        case LIBINPUT_CONFIG_TAP_DISABLED:
            tap = "disabled";
            break;
        }
        json_object_object_add(object, "tap", json_object_new_string(tap));

        const char *button_map = "unknown";
        switch (libinput_device_config_tap_get_button_map(device)) {
        case LIBINPUT_CONFIG_TAP_MAP_LRM:
            button_map = "lrm";
            break;
        case LIBINPUT_CONFIG_TAP_MAP_LMR:
            button_map = "lmr";
            break;
        }
        json_object_object_add(
            object, "tap_button_map", json_object_new_string(button_map)
        );

        const char *drag = "unknown";
        switch (libinput_device_config_tap_get_drag_enabled(device)) {
        case LIBINPUT_CONFIG_DRAG_ENABLED:
            drag = "enabled";
            break;
        case LIBINPUT_CONFIG_DRAG_DISABLED:
            drag = "disabled";
            break;
        }
        json_object_object_add(
            object, "tap_drag", json_object_new_string(drag)
        );

        const char *drag_lock = "unknown";
        switch (libinput_device_config_tap_get_drag_lock_enabled(device)) {
        case LIBINPUT_CONFIG_DRAG_LOCK_ENABLED:
            drag_lock = "enabled";
            break;
        case LIBINPUT_CONFIG_DRAG_LOCK_DISABLED:
            drag_lock = "disabled";
            break;
        }
        json_object_object_add(
            object, "tap_drag_lock", json_object_new_string(drag_lock)
        );
    }

    if (libinput_device_config_accel_is_available(device)) {
        double accel = libinput_device_config_accel_get_speed(device);
        json_object_object_add(
            object, "accel_speed", json_object_new_double(accel)
        );

        const char *accel_profile = "unknown";
        switch (libinput_device_config_accel_get_profile(device)) {
        case LIBINPUT_CONFIG_ACCEL_PROFILE_NONE:
            accel_profile = "none";
            break;
        case LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT:
            accel_profile = "flat";
            break;
        case LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE:
            accel_profile = "adaptive";
            break;
        default:
            break;
        }
        json_object_object_add(
            object, "accel_profile", json_object_new_string(accel_profile)
        );
    }

    if (libinput_device_config_scroll_has_natural_scroll(device)) {
        const char *natural_scroll = "disabled";
        if (libinput_device_config_scroll_get_natural_scroll_enabled(device)) {
            natural_scroll = "enabled";
        }
        json_object_object_add(
            object, "natural_scroll", json_object_new_string(natural_scroll)
        );
    }

    if (libinput_device_config_left_handed_is_available(device)) {
        const char *left_handed = "disabled";
        if (libinput_device_config_left_handed_get(device) != 0) {
            left_handed = "enabled";
        }
        json_object_object_add(
            object, "left_handed", json_object_new_string(left_handed)
        );
    }

    uint32_t click_methods = libinput_device_config_click_get_methods(device);
    if ((click_methods & ~LIBINPUT_CONFIG_CLICK_METHOD_NONE) != 0) {
        const char *click_method = "unknown";
        switch (libinput_device_config_click_get_method(device)) {
        case LIBINPUT_CONFIG_CLICK_METHOD_NONE:
            click_method = "none";
            break;
        case LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS:
            click_method = "button_areas";
            break;
        case LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER:
            click_method = "clickfinger";
            break;
        }
        json_object_object_add(
            object, "click_method", json_object_new_string(click_method)
        );
    }

    if (libinput_device_config_middle_emulation_is_available(device)) {
        const char *middle_emulation = "unknown";
        switch (libinput_device_config_middle_emulation_get_enabled(device)) {
        case LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED:
            middle_emulation = "enabled";
            break;
        case LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED:
            middle_emulation = "disabled";
            break;
        }
        json_object_object_add(
            object, "middle_emulation", json_object_new_string(middle_emulation)
        );
    }

    uint32_t scroll_methods = libinput_device_config_scroll_get_methods(device);
    if ((scroll_methods & ~LIBINPUT_CONFIG_SCROLL_NO_SCROLL) != 0) {
        const char *scroll_method = "unknown";
        switch (libinput_device_config_scroll_get_method(device)) {
        case LIBINPUT_CONFIG_SCROLL_NO_SCROLL:
            scroll_method = "none";
            break;
        case LIBINPUT_CONFIG_SCROLL_2FG:
            scroll_method = "two_finger";
            break;
        case LIBINPUT_CONFIG_SCROLL_EDGE:
            scroll_method = "edge";
            break;
        case LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN:
            scroll_method = "on_button_down";
            break;
        }
        json_object_object_add(
            object, "scroll_method", json_object_new_string(scroll_method)
        );

        if ((scroll_methods & LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN) != 0) {
            uint32_t button = libinput_device_config_scroll_get_button(device);
            json_object_object_add(
                object, "scroll_button", json_object_new_int(button)
            );
        }
    }

    if (libinput_device_config_dwt_is_available(device)) {
        const char *dwt = "unknown";
        switch (libinput_device_config_dwt_get_enabled(device)) {
        case LIBINPUT_CONFIG_DWT_ENABLED:
            dwt = "enabled";
            break;
        case LIBINPUT_CONFIG_DWT_DISABLED:
            dwt = "disabled";
            break;
        }
        json_object_object_add(object, "dwt", json_object_new_string(dwt));
    }

    if (libinput_device_config_calibration_has_matrix(device)) {
        float matrix[6];
        libinput_device_config_calibration_get_matrix(device, matrix);
        struct json_object *array = json_object_new_array();
        struct json_object *x;
        for (int i = 0; i < 6; i++) {
            x = json_object_new_double(matrix[i]);
            json_object_array_add(array, x);
        }
        json_object_object_add(object, "calibration_matrix", array);
    }

    return object;
}

json_object *
ipc_json_describe_input(struct hayward_input_device *device) {
    hayward_assert(device, "Device must not be null");

    json_object *object = json_object_new_object();

    json_object_object_add(
        object, "identifier", json_object_new_string(device->identifier)
    );
    json_object_object_add(
        object, "name", json_object_new_string(device->wlr_device->name)
    );
    json_object_object_add(
        object, "vendor", json_object_new_int(device->wlr_device->vendor)
    );
    json_object_object_add(
        object, "product", json_object_new_int(device->wlr_device->product)
    );
    json_object_object_add(
        object, "type", json_object_new_string(input_device_get_type(device))
    );

    if (device->wlr_device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct wlr_keyboard *keyboard =
            wlr_keyboard_from_input_device(device->wlr_device);
        struct xkb_keymap *keymap = keyboard->keymap;
        struct xkb_state *state = keyboard->xkb_state;

        json_object_object_add(
            object, "repeat_delay",
            json_object_new_int(keyboard->repeat_info.delay)
        );
        json_object_object_add(
            object, "repeat_rate",
            json_object_new_int(keyboard->repeat_info.rate)
        );

        json_object *layouts_arr = json_object_new_array();
        json_object_object_add(object, "xkb_layout_names", layouts_arr);

        xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(keymap);
        xkb_layout_index_t layout_idx;
        for (layout_idx = 0; layout_idx < num_layouts; layout_idx++) {
            const char *layout = xkb_keymap_layout_get_name(keymap, layout_idx);
            json_object_array_add(
                layouts_arr, layout ? json_object_new_string(layout) : NULL
            );

            bool is_active = xkb_state_layout_index_is_active(
                state, layout_idx, XKB_STATE_LAYOUT_EFFECTIVE
            );
            if (is_active) {
                json_object_object_add(
                    object, "xkb_active_layout_index",
                    json_object_new_int(layout_idx)
                );
                json_object_object_add(
                    object, "xkb_active_layout_name",
                    layout ? json_object_new_string(layout) : NULL
                );
            }
        }
    }

    if (device->wlr_device->type == WLR_INPUT_DEVICE_POINTER) {
        struct input_config *ic = input_device_get_config(device);
        float scroll_factor = 1.0f;
        if (ic != NULL && !isnan(ic->scroll_factor) &&
            ic->scroll_factor != FLT_MIN) {
            scroll_factor = ic->scroll_factor;
        }
        json_object_object_add(
            object, "scroll_factor", json_object_new_double(scroll_factor)
        );
    }

    if (wlr_input_device_is_libinput(device->wlr_device)) {
        struct libinput_device *libinput_dev;
        libinput_dev = wlr_libinput_get_device_handle(device->wlr_device);
        json_object_object_add(
            object, "libinput", describe_libinput_device(libinput_dev)
        );
    }

    return object;
}

json_object *
ipc_json_describe_seat(struct hayward_seat *seat) {
    hayward_assert(seat, "Seat must not be null");

    json_object *object = json_object_new_object();

    json_object_object_add(
        object, "name", json_object_new_string(seat->wlr_seat->name)
    );
    json_object_object_add(
        object, "capabilities",
        json_object_new_int(seat->wlr_seat->capabilities)
    );

    json_object *devices = json_object_new_array();
    struct hayward_seat_device *device = NULL;
    wl_list_for_each(device, &seat->devices, link) {
        json_object_array_add(
            devices, ipc_json_describe_input(device->input_device)
        );
    }
    json_object_object_add(object, "devices", devices);

    return object;
}

static uint32_t
event_to_x11_button(uint32_t event) {
    switch (event) {
    case BTN_LEFT:
        return 1;
    case BTN_MIDDLE:
        return 2;
    case BTN_RIGHT:
        return 3;
    case HAYWARD_SCROLL_UP:
        return 4;
    case HAYWARD_SCROLL_DOWN:
        return 5;
    case HAYWARD_SCROLL_LEFT:
        return 6;
    case HAYWARD_SCROLL_RIGHT:
        return 7;
    case BTN_SIDE:
        return 8;
    case BTN_EXTRA:
        return 9;
    default:
        return 0;
    }
}

json_object *
ipc_json_describe_bar_config(struct bar_config *bar) {
    hayward_assert(bar, "Bar must not be NULL");

    json_object *json = json_object_new_object();
    json_object_object_add(json, "id", json_object_new_string(bar->id));
    json_object_object_add(json, "mode", json_object_new_string(bar->mode));
    json_object_object_add(
        json, "hidden_state", json_object_new_string(bar->hidden_state)
    );
    json_object_object_add(
        json, "position", json_object_new_string(bar->position)
    );
    json_object_object_add(
        json, "status_command",
        bar->status_command ? json_object_new_string(bar->status_command) : NULL
    );
    json_object_object_add(
        json, "font",
        json_object_new_string((bar->font) ? bar->font : config->font)
    );

    json_object *gaps = json_object_new_object();
    json_object_object_add(gaps, "top", json_object_new_int(bar->gaps.top));
    json_object_object_add(gaps, "right", json_object_new_int(bar->gaps.right));
    json_object_object_add(
        gaps, "bottom", json_object_new_int(bar->gaps.bottom)
    );
    json_object_object_add(gaps, "left", json_object_new_int(bar->gaps.left));
    json_object_object_add(json, "gaps", gaps);

    if (bar->separator_symbol) {
        json_object_object_add(
            json, "separator_symbol",
            json_object_new_string(bar->separator_symbol)
        );
    }
    json_object_object_add(
        json, "bar_height", json_object_new_int(bar->height)
    );
    json_object_object_add(
        json, "status_padding", json_object_new_int(bar->status_padding)
    );
    json_object_object_add(
        json, "status_edge_padding",
        json_object_new_int(bar->status_edge_padding)
    );
    json_object_object_add(
        json, "wrap_scroll", json_object_new_boolean(bar->wrap_scroll)
    );
    json_object_object_add(
        json, "workspace_buttons",
        json_object_new_boolean(bar->workspace_buttons)
    );
    json_object_object_add(
        json, "strip_workspace_numbers",
        json_object_new_boolean(bar->strip_workspace_numbers)
    );
    json_object_object_add(
        json, "strip_workspace_name",
        json_object_new_boolean(bar->strip_workspace_name)
    );
    json_object_object_add(
        json, "workspace_min_width",
        json_object_new_int(bar->workspace_min_width)
    );
    json_object_object_add(
        json, "binding_mode_indicator",
        json_object_new_boolean(bar->binding_mode_indicator)
    );
    json_object_object_add(
        json, "verbose", json_object_new_boolean(bar->verbose)
    );
    json_object_object_add(
        json, "pango_markup",
        json_object_new_boolean(
            bar->pango_markup == PANGO_MARKUP_DEFAULT ? config->pango_markup
                                                      : bar->pango_markup
        )
    );

    json_object *colors = json_object_new_object();
    json_object_object_add(
        colors, "background", json_object_new_string(bar->colors.background)
    );
    json_object_object_add(
        colors, "statusline", json_object_new_string(bar->colors.statusline)
    );
    json_object_object_add(
        colors, "separator", json_object_new_string(bar->colors.separator)
    );

    if (bar->colors.focused_background) {
        json_object_object_add(
            colors, "focused_background",
            json_object_new_string(bar->colors.focused_background)
        );
    } else {
        json_object_object_add(
            colors, "focused_background",
            json_object_new_string(bar->colors.background)
        );
    }

    if (bar->colors.focused_statusline) {
        json_object_object_add(
            colors, "focused_statusline",
            json_object_new_string(bar->colors.focused_statusline)
        );
    } else {
        json_object_object_add(
            colors, "focused_statusline",
            json_object_new_string(bar->colors.statusline)
        );
    }

    if (bar->colors.focused_separator) {
        json_object_object_add(
            colors, "focused_separator",
            json_object_new_string(bar->colors.focused_separator)
        );
    } else {
        json_object_object_add(
            colors, "focused_separator",
            json_object_new_string(bar->colors.separator)
        );
    }

    json_object_object_add(
        colors, "focused_workspace_border",
        json_object_new_string(bar->colors.focused_workspace_border)
    );
    json_object_object_add(
        colors, "focused_workspace_bg",
        json_object_new_string(bar->colors.focused_workspace_bg)
    );
    json_object_object_add(
        colors, "focused_workspace_text",
        json_object_new_string(bar->colors.focused_workspace_text)
    );

    json_object_object_add(
        colors, "inactive_workspace_border",
        json_object_new_string(bar->colors.inactive_workspace_border)
    );
    json_object_object_add(
        colors, "inactive_workspace_bg",
        json_object_new_string(bar->colors.inactive_workspace_bg)
    );
    json_object_object_add(
        colors, "inactive_workspace_text",
        json_object_new_string(bar->colors.inactive_workspace_text)
    );

    json_object_object_add(
        colors, "active_workspace_border",
        json_object_new_string(bar->colors.active_workspace_border)
    );
    json_object_object_add(
        colors, "active_workspace_bg",
        json_object_new_string(bar->colors.active_workspace_bg)
    );
    json_object_object_add(
        colors, "active_workspace_text",
        json_object_new_string(bar->colors.active_workspace_text)
    );

    json_object_object_add(
        colors, "urgent_workspace_border",
        json_object_new_string(bar->colors.urgent_workspace_border)
    );
    json_object_object_add(
        colors, "urgent_workspace_bg",
        json_object_new_string(bar->colors.urgent_workspace_bg)
    );
    json_object_object_add(
        colors, "urgent_workspace_text",
        json_object_new_string(bar->colors.urgent_workspace_text)
    );

    if (bar->colors.binding_mode_border) {
        json_object_object_add(
            colors, "binding_mode_border",
            json_object_new_string(bar->colors.binding_mode_border)
        );
    } else {
        json_object_object_add(
            colors, "binding_mode_border",
            json_object_new_string(bar->colors.urgent_workspace_border)
        );
    }

    if (bar->colors.binding_mode_bg) {
        json_object_object_add(
            colors, "binding_mode_bg",
            json_object_new_string(bar->colors.binding_mode_bg)
        );
    } else {
        json_object_object_add(
            colors, "binding_mode_bg",
            json_object_new_string(bar->colors.urgent_workspace_bg)
        );
    }

    if (bar->colors.binding_mode_text) {
        json_object_object_add(
            colors, "binding_mode_text",
            json_object_new_string(bar->colors.binding_mode_text)
        );
    } else {
        json_object_object_add(
            colors, "binding_mode_text",
            json_object_new_string(bar->colors.urgent_workspace_text)
        );
    }

    json_object_object_add(json, "colors", colors);

    if (bar->bindings->length > 0) {
        json_object *bindings = json_object_new_array();
        for (int i = 0; i < bar->bindings->length; ++i) {
            struct bar_binding *binding = bar->bindings->items[i];
            json_object *bind = json_object_new_object();
            json_object_object_add(
                bind, "input_code",
                json_object_new_int(event_to_x11_button(binding->button))
            );
            json_object_object_add(
                bind, "event_code", json_object_new_int(binding->button)
            );
            json_object_object_add(
                bind, "command", json_object_new_string(binding->command)
            );
            json_object_object_add(
                bind, "release", json_object_new_boolean(binding->release)
            );
            json_object_array_add(bindings, bind);
        }
        json_object_object_add(json, "bindings", bindings);
    }

    // Add outputs if defined
    if (bar->outputs && bar->outputs->length > 0) {
        json_object *outputs = json_object_new_array();
        for (int i = 0; i < bar->outputs->length; ++i) {
            const char *name = bar->outputs->items[i];
            json_object_array_add(outputs, json_object_new_string(name));
        }
        json_object_object_add(json, "outputs", outputs);
    }
#if HAVE_TRAY
    // Add tray outputs if defined
    if (bar->tray_outputs && bar->tray_outputs->length > 0) {
        json_object *tray_outputs = json_object_new_array();
        for (int i = 0; i < bar->tray_outputs->length; ++i) {
            const char *name = bar->tray_outputs->items[i];
            json_object_array_add(tray_outputs, json_object_new_string(name));
        }
        json_object_object_add(json, "tray_outputs", tray_outputs);
    }

    json_object *tray_bindings = json_object_new_array();
    struct tray_binding *tray_bind = NULL;
    wl_list_for_each(tray_bind, &bar->tray_bindings, link) {
        json_object *bind = json_object_new_object();
        json_object_object_add(
            bind, "input_code",
            json_object_new_int(event_to_x11_button(tray_bind->button))
        );
        json_object_object_add(
            bind, "event_code", json_object_new_int(tray_bind->button)
        );
        json_object_object_add(
            bind, "command", json_object_new_string(tray_bind->command)
        );
        json_object_array_add(tray_bindings, bind);
    }
    if (json_object_array_length(tray_bindings) > 0) {
        json_object_object_add(json, "tray_bindings", tray_bindings);
    } else {
        json_object_put(tray_bindings);
    }

    if (bar->icon_theme) {
        json_object_object_add(
            json, "icon_theme", json_object_new_string(bar->icon_theme)
        );
    }

    json_object_object_add(
        json, "tray_padding", json_object_new_int(bar->tray_padding)
    );
#endif
    return json;
}
