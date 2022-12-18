#define _POSIX_C_SOURCE 200809L
#include "haywardnag/haywardnag.h"

#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "hayward-common/list.h"
#include "hayward-common/log.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "haywardnag/render.h"
#include "haywardnag/types.h"

static void
nop() {
    // Intentionally left blank
}

static bool
terminal_execute(char *terminal, char *command) {
    char fname[] = "/tmp/haywardnagXXXXXX";
    FILE *tmp = fdopen(mkstemp(fname), "w");
    if (!tmp) {
        hayward_log(HAYWARD_ERROR, "Failed to create temp script");
        return false;
    }
    hayward_log(HAYWARD_DEBUG, "Created temp script: %s", fname);
    fprintf(tmp, "#!/bin/sh\nrm %s\n%s", fname, command);
    fclose(tmp);
    chmod(fname, S_IRUSR | S_IWUSR | S_IXUSR);
    size_t cmd_size = strlen(terminal) + strlen(" -e ") + strlen(fname) + 1;
    char *cmd = malloc(cmd_size);
    if (!cmd) {
        perror("malloc");
        return false;
    }
    snprintf(cmd, cmd_size, "%s -e %s", terminal, fname);
    execlp("sh", "sh", "-c", cmd, NULL);
    hayward_log_errno(
        HAYWARD_ERROR, "Failed to run command, execlp() returned."
    );
    free(cmd);
    return false;
}

static void
haywardnag_button_execute(
    struct haywardnag *haywardnag, struct haywardnag_button *button
) {
    hayward_log(
        HAYWARD_DEBUG, "Executing [%s]: %s", button->text, button->action
    );
    if (button->type == HAYWARDNAG_ACTION_DISMISS) {
        haywardnag->run_display = false;
    } else if (button->type == HAYWARDNAG_ACTION_EXPAND) {
        haywardnag->details.visible = !haywardnag->details.visible;
        render_frame(haywardnag);
    } else {
        pid_t pid = fork();
        if (pid < 0) {
            hayward_log_errno(HAYWARD_DEBUG, "Failed to fork");
            return;
        } else if (pid == 0) {
            // Child process. Will be used to prevent zombie processes
            pid = fork();
            if (pid < 0) {
                hayward_log_errno(HAYWARD_DEBUG, "Failed to fork");
                return;
            } else if (pid == 0) {
                // Child of the child. Will be reparented to the init process
                char *terminal = getenv("TERMINAL");
                if (button->terminal && terminal && *terminal) {
                    hayward_log(HAYWARD_DEBUG, "Found $TERMINAL: %s", terminal);
                    if (!terminal_execute(terminal, button->action)) {
                        haywardnag_destroy(haywardnag);
                        _exit(EXIT_FAILURE);
                    }
                } else {
                    if (button->terminal) {
                        hayward_log(
                            HAYWARD_DEBUG,
                            "$TERMINAL not found. Running directly"
                        );
                    }
                    execlp("sh", "sh", "-c", button->action, NULL);
                    hayward_log_errno(HAYWARD_DEBUG, "execlp failed");
                    _exit(EXIT_FAILURE);
                }
            }
            _exit(EXIT_SUCCESS);
        }

        if (button->dismiss) {
            haywardnag->run_display = false;
        }

        if (waitpid(pid, NULL, 0) < 0) {
            hayward_log_errno(HAYWARD_DEBUG, "waitpid failed");
        }
    }
}

static void
layer_surface_configure(
    void *data, struct zwlr_layer_surface_v1 *surface, uint32_t serial,
    uint32_t width, uint32_t height
) {
    struct haywardnag *haywardnag = data;
    haywardnag->width = width;
    haywardnag->height = height;
    zwlr_layer_surface_v1_ack_configure(surface, serial);
    render_frame(haywardnag);
}

static void
layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    struct haywardnag *haywardnag = data;
    haywardnag_destroy(haywardnag);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void
surface_enter(
    void *data, struct wl_surface *surface, struct wl_output *output
) {
    struct haywardnag *haywardnag = data;
    struct haywardnag_output *haywardnag_output;
    wl_list_for_each(haywardnag_output, &haywardnag->outputs, link) {
        if (haywardnag_output->wl_output == output) {
            hayward_log(
                HAYWARD_DEBUG, "Surface enter on output %s",
                haywardnag_output->name
            );
            haywardnag->output = haywardnag_output;
            haywardnag->scale = haywardnag->output->scale;
            render_frame(haywardnag);
            break;
        }
    };
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = nop,
};

static void
update_cursor(struct haywardnag_seat *seat) {
    struct haywardnag_pointer *pointer = &seat->pointer;
    struct haywardnag *haywardnag = seat->haywardnag;
    if (pointer->cursor_theme) {
        wl_cursor_theme_destroy(pointer->cursor_theme);
    }
    const char *cursor_theme = getenv("XCURSOR_THEME");
    unsigned cursor_size = 24;
    const char *env_cursor_size = getenv("XCURSOR_SIZE");
    if (env_cursor_size && *env_cursor_size) {
        errno = 0;
        char *end;
        unsigned size = strtoul(env_cursor_size, &end, 10);
        if (!*end && errno == 0) {
            cursor_size = size;
        }
    }
    pointer->cursor_theme = wl_cursor_theme_load(
        cursor_theme, cursor_size * haywardnag->scale, haywardnag->shm
    );
    struct wl_cursor *cursor =
        wl_cursor_theme_get_cursor(pointer->cursor_theme, "left_ptr");
    pointer->cursor_image = cursor->images[0];
    wl_surface_set_buffer_scale(pointer->cursor_surface, haywardnag->scale);
    wl_surface_attach(
        pointer->cursor_surface,
        wl_cursor_image_get_buffer(pointer->cursor_image), 0, 0
    );
    wl_pointer_set_cursor(
        pointer->pointer, pointer->serial, pointer->cursor_surface,
        pointer->cursor_image->hotspot_x / haywardnag->scale,
        pointer->cursor_image->hotspot_y / haywardnag->scale
    );
    wl_surface_damage_buffer(
        pointer->cursor_surface, 0, 0, INT32_MAX, INT32_MAX
    );
    wl_surface_commit(pointer->cursor_surface);
}

void
update_all_cursors(struct haywardnag *haywardnag) {
    struct haywardnag_seat *seat;
    wl_list_for_each(seat, &haywardnag->seats, link) {
        if (seat->pointer.pointer) {
            update_cursor(seat);
        }
    }
}

static void
wl_pointer_enter(
    void *data, struct wl_pointer *wl_pointer, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y
) {
    struct haywardnag_seat *seat = data;
    struct haywardnag_pointer *pointer = &seat->pointer;
    pointer->x = wl_fixed_to_int(surface_x);
    pointer->y = wl_fixed_to_int(surface_y);
    pointer->serial = serial;
    update_cursor(seat);
}

static void
wl_pointer_motion(
    void *data, struct wl_pointer *wl_pointer, uint32_t time,
    wl_fixed_t surface_x, wl_fixed_t surface_y
) {
    struct haywardnag_seat *seat = data;
    seat->pointer.x = wl_fixed_to_int(surface_x);
    seat->pointer.y = wl_fixed_to_int(surface_y);
}

static void
wl_pointer_button(
    void *data, struct wl_pointer *wl_pointer, uint32_t serial, uint32_t time,
    uint32_t button, uint32_t state
) {
    struct haywardnag_seat *seat = data;
    struct haywardnag *haywardnag = seat->haywardnag;

    if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
        return;
    }

    double x = seat->pointer.x;
    double y = seat->pointer.y;
    for (int i = 0; i < haywardnag->buttons->length; i++) {
        struct haywardnag_button *nagbutton = haywardnag->buttons->items[i];
        if (x >= nagbutton->x && y >= nagbutton->y &&
            x < nagbutton->x + nagbutton->width &&
            y < nagbutton->y + nagbutton->height) {
            haywardnag_button_execute(haywardnag, nagbutton);
            return;
        }
    }

    if (haywardnag->details.visible &&
        haywardnag->details.total_lines != haywardnag->details.visible_lines) {
        struct haywardnag_button button_up = haywardnag->details.button_up;
        if (x >= button_up.x && y >= button_up.y &&
            x < button_up.x + button_up.width &&
            y < button_up.y + button_up.height &&
            haywardnag->details.offset > 0) {
            haywardnag->details.offset--;
            render_frame(haywardnag);
            return;
        }

        struct haywardnag_button button_down = haywardnag->details.button_down;
        int bot = haywardnag->details.total_lines;
        bot -= haywardnag->details.visible_lines;
        if (x >= button_down.x && y >= button_down.y &&
            x < button_down.x + button_down.width &&
            y < button_down.y + button_down.height &&
            haywardnag->details.offset < bot) {
            haywardnag->details.offset++;
            render_frame(haywardnag);
            return;
        }
    }
}

static void
wl_pointer_axis(
    void *data, struct wl_pointer *wl_pointer, uint32_t time, uint32_t axis,
    wl_fixed_t value
) {
    struct haywardnag_seat *seat = data;
    struct haywardnag *haywardnag = seat->haywardnag;
    if (!haywardnag->details.visible ||
        seat->pointer.x < haywardnag->details.x ||
        seat->pointer.y < haywardnag->details.y ||
        seat->pointer.x >= haywardnag->details.x + haywardnag->details.width ||
        seat->pointer.y >= haywardnag->details.y + haywardnag->details.height ||
        haywardnag->details.total_lines == haywardnag->details.visible_lines) {
        return;
    }

    int direction = wl_fixed_to_int(value);
    int bot =
        haywardnag->details.total_lines - haywardnag->details.visible_lines;
    if (direction < 0 && haywardnag->details.offset > 0) {
        haywardnag->details.offset--;
    } else if (direction > 0 && haywardnag->details.offset < bot) {
        haywardnag->details.offset++;
    }

    render_frame(haywardnag);
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = wl_pointer_enter,
    .leave = nop,
    .motion = wl_pointer_motion,
    .button = wl_pointer_button,
    .axis = wl_pointer_axis,
    .frame = nop,
    .axis_source = nop,
    .axis_stop = nop,
    .axis_discrete = nop,
};

static void
seat_handle_capabilities(
    void *data, struct wl_seat *wl_seat, enum wl_seat_capability caps
) {
    struct haywardnag_seat *seat = data;
    bool cap_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
    if (cap_pointer && !seat->pointer.pointer) {
        seat->pointer.pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(seat->pointer.pointer, &pointer_listener, seat);
    } else if (!cap_pointer && seat->pointer.pointer) {
        wl_pointer_destroy(seat->pointer.pointer);
        seat->pointer.pointer = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = nop,
};

static void
output_scale(void *data, struct wl_output *output, int32_t factor) {
    struct haywardnag_output *haywardnag_output = data;
    haywardnag_output->scale = factor;
    if (haywardnag_output->haywardnag->output == haywardnag_output) {
        haywardnag_output->haywardnag->scale = haywardnag_output->scale;
        update_all_cursors(haywardnag_output->haywardnag);
        render_frame(haywardnag_output->haywardnag);
    }
}

static void
output_name(void *data, struct wl_output *output, const char *name) {
    struct haywardnag_output *haywardnag_output = data;
    haywardnag_output->name = strdup(name);

    const char *outname = haywardnag_output->haywardnag->type->output;
    if (!haywardnag_output->haywardnag->output && outname &&
        strcmp(outname, name) == 0) {
        hayward_log(HAYWARD_DEBUG, "Using output %s", name);
        haywardnag_output->haywardnag->output = haywardnag_output;
    }
}

static const struct wl_output_listener output_listener = {
    .geometry = nop,
    .mode = nop,
    .done = nop,
    .scale = output_scale,
    .name = output_name,
    .description = nop,
};

static void
handle_global(
    void *data, struct wl_registry *registry, uint32_t name,
    const char *interface, uint32_t version
) {
    struct haywardnag *haywardnag = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        haywardnag->compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        struct haywardnag_seat *seat =
            calloc(1, sizeof(struct haywardnag_seat));
        if (!seat) {
            perror("calloc");
            return;
        }

        seat->haywardnag = haywardnag;
        seat->wl_name = name;
        seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);

        wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);

        wl_list_insert(&haywardnag->seats, &seat->link);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        haywardnag->shm =
            wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        if (!haywardnag->output) {
            struct haywardnag_output *output =
                calloc(1, sizeof(struct haywardnag_output));
            if (!output) {
                perror("calloc");
                return;
            }
            output->wl_output =
                wl_registry_bind(registry, name, &wl_output_interface, 4);
            output->wl_name = name;
            output->scale = 1;
            output->haywardnag = haywardnag;
            wl_list_insert(&haywardnag->outputs, &output->link);
            wl_output_add_listener(output->wl_output, &output_listener, output);
        }
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        haywardnag->layer_shell =
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    }
}

void
haywardnag_seat_destroy(struct haywardnag_seat *seat) {
    if (seat->pointer.cursor_theme) {
        wl_cursor_theme_destroy(seat->pointer.cursor_theme);
    }
    if (seat->pointer.pointer) {
        wl_pointer_destroy(seat->pointer.pointer);
    }
    wl_seat_destroy(seat->wl_seat);
    wl_list_remove(&seat->link);
    free(seat);
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    struct haywardnag *haywardnag = data;
    if (haywardnag->output->wl_name == name) {
        haywardnag->run_display = false;
    }

    struct haywardnag_seat *seat, *tmpseat;
    wl_list_for_each_safe(seat, tmpseat, &haywardnag->seats, link) {
        if (seat->wl_name == name) {
            haywardnag_seat_destroy(seat);
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

void
haywardnag_setup_cursors(struct haywardnag *haywardnag) {
    struct haywardnag_seat *seat;

    wl_list_for_each(seat, &haywardnag->seats, link) {
        struct haywardnag_pointer *p = &seat->pointer;

        p->cursor_surface =
            wl_compositor_create_surface(haywardnag->compositor);
        assert(p->cursor_surface);
    }
}

void
haywardnag_setup(struct haywardnag *haywardnag) {
    haywardnag->display = wl_display_connect(NULL);
    if (!haywardnag->display) {
        hayward_abort("Unable to connect to the compositor. "
                      "If your compositor is running, check or set the "
                      "WAYLAND_DISPLAY environment variable.");
    }

    haywardnag->scale = 1;

    struct wl_registry *registry = wl_display_get_registry(haywardnag->display);
    wl_registry_add_listener(registry, &registry_listener, haywardnag);
    if (wl_display_roundtrip(haywardnag->display) < 0) {
        hayward_abort("failed to register with the wayland display");
    }

    assert(
        haywardnag->compositor && haywardnag->layer_shell && haywardnag->shm
    );

    // Second roundtrip to get wl_output properties
    if (wl_display_roundtrip(haywardnag->display) < 0) {
        hayward_log(HAYWARD_ERROR, "Error during outputs init.");
        haywardnag_destroy(haywardnag);
        exit(EXIT_FAILURE);
    }

    if (!haywardnag->output && haywardnag->type->output) {
        hayward_log(
            HAYWARD_ERROR, "Output '%s' not found", haywardnag->type->output
        );
        haywardnag_destroy(haywardnag);
        exit(EXIT_FAILURE);
    }

    haywardnag_setup_cursors(haywardnag);

    haywardnag->surface = wl_compositor_create_surface(haywardnag->compositor);
    assert(haywardnag->surface);
    wl_surface_add_listener(haywardnag->surface, &surface_listener, haywardnag);

    haywardnag->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        haywardnag->layer_shell, haywardnag->surface,
        haywardnag->output ? haywardnag->output->wl_output : NULL,
        haywardnag->type->layer, "haywardnag"
    );
    assert(haywardnag->layer_surface);
    zwlr_layer_surface_v1_add_listener(
        haywardnag->layer_surface, &layer_surface_listener, haywardnag
    );
    zwlr_layer_surface_v1_set_anchor(
        haywardnag->layer_surface, haywardnag->type->anchors
    );

    wl_registry_destroy(registry);
}

void
haywardnag_run(struct haywardnag *haywardnag) {
    haywardnag->run_display = true;
    render_frame(haywardnag);
    while (haywardnag->run_display &&
           wl_display_dispatch(haywardnag->display) != -1) {
        // This is intentionally left blank
    }

    if (haywardnag->display) {
        wl_display_disconnect(haywardnag->display);
    }
}

void
haywardnag_destroy(struct haywardnag *haywardnag) {
    haywardnag->run_display = false;

    free(haywardnag->message);
    for (int i = 0; i < haywardnag->buttons->length; ++i) {
        struct haywardnag_button *button = haywardnag->buttons->items[i];
        free(button->text);
        free(button->action);
        free(button);
    }
    list_free(haywardnag->buttons);
    free(haywardnag->details.message);
    free(haywardnag->details.button_up.text);
    free(haywardnag->details.button_down.text);

    if (haywardnag->type) {
        haywardnag_type_free(haywardnag->type);
    }

    if (haywardnag->layer_surface) {
        zwlr_layer_surface_v1_destroy(haywardnag->layer_surface);
    }

    if (haywardnag->surface) {
        wl_surface_destroy(haywardnag->surface);
    }

    struct haywardnag_seat *seat, *tmpseat;
    wl_list_for_each_safe(seat, tmpseat, &haywardnag->seats, link) {
        haywardnag_seat_destroy(seat);
    }

    destroy_buffer(&haywardnag->buffers[0]);
    destroy_buffer(&haywardnag->buffers[1]);

    if (haywardnag->outputs.prev || haywardnag->outputs.next) {
        struct haywardnag_output *output, *temp;
        wl_list_for_each_safe(output, temp, &haywardnag->outputs, link) {
            wl_output_destroy(output->wl_output);
            free(output->name);
            wl_list_remove(&output->link);
            free(output);
        };
    }

    if (haywardnag->compositor) {
        wl_compositor_destroy(haywardnag->compositor);
    }

    if (haywardnag->shm) {
        wl_shm_destroy(haywardnag->shm);
    }
}
