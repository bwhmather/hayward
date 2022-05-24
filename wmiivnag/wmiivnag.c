#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "log.h"
#include "list.h"
#include "wmiivnag/render.h"
#include "wmiivnag/wmiivnag.h"
#include "wmiivnag/types.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static void nop() {
	// Intentionally left blank
}

static bool terminal_execute(char *terminal, char *command) {
	char fname[] = "/tmp/wmiivnagXXXXXX";
	FILE *tmp= fdopen(mkstemp(fname), "w");
	if (!tmp) {
		wmiiv_log(WMIIV_ERROR, "Failed to create temp script");
		return false;
	}
	wmiiv_log(WMIIV_DEBUG, "Created temp script: %s", fname);
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
	wmiiv_log_errno(WMIIV_ERROR, "Failed to run command, execlp() returned.");
	free(cmd);
	return false;
}

static void wmiivnag_button_execute(struct wmiivnag *wmiivnag,
		struct wmiivnag_button *button) {
	wmiiv_log(WMIIV_DEBUG, "Executing [%s]: %s", button->text, button->action);
	if (button->type == WMIIVNAG_ACTION_DISMISS) {
		wmiivnag->run_display = false;
	} else if (button->type == WMIIVNAG_ACTION_EXPAND) {
		wmiivnag->details.visible = !wmiivnag->details.visible;
		render_frame(wmiivnag);
	} else {
		pid_t pid = fork();
		if (pid < 0) {
			wmiiv_log_errno(WMIIV_DEBUG, "Failed to fork");
			return;
		} else if (pid == 0) {
			// Child process. Will be used to prevent zombie processes
			pid = fork();
			if (pid < 0) {
				wmiiv_log_errno(WMIIV_DEBUG, "Failed to fork");
				return;
			} else if (pid == 0) {
				// Child of the child. Will be reparented to the init process
				char *terminal = getenv("TERMINAL");
				if (button->terminal && terminal && *terminal) {
					wmiiv_log(WMIIV_DEBUG, "Found $TERMINAL: %s", terminal);
					if (!terminal_execute(terminal, button->action)) {
						wmiivnag_destroy(wmiivnag);
						_exit(EXIT_FAILURE);
					}
				} else {
					if (button->terminal) {
						wmiiv_log(WMIIV_DEBUG,
								"$TERMINAL not found. Running directly");
					}
					execlp("sh", "sh", "-c", button->action, NULL);
					wmiiv_log_errno(WMIIV_DEBUG, "execlp failed");
					_exit(EXIT_FAILURE);
				}
			}
			_exit(EXIT_SUCCESS);
		}

		if (button->dismiss) {
			wmiivnag->run_display = false;
		}

		if (waitpid(pid, NULL, 0) < 0) {
			wmiiv_log_errno(WMIIV_DEBUG, "waitpid failed");
		}
	}
}

static void layer_surface_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct wmiivnag *wmiivnag = data;
	wmiivnag->width = width;
	wmiivnag->height = height;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	render_frame(wmiivnag);
}

static void layer_surface_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	struct wmiivnag *wmiivnag = data;
	wmiivnag_destroy(wmiivnag);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static void surface_enter(void *data, struct wl_surface *surface,
		struct wl_output *output) {
	struct wmiivnag *wmiivnag = data;
	struct wmiivnag_output *wmiivnag_output;
	wl_list_for_each(wmiivnag_output, &wmiivnag->outputs, link) {
		if (wmiivnag_output->wl_output == output) {
			wmiiv_log(WMIIV_DEBUG, "Surface enter on output %s",
					wmiivnag_output->name);
			wmiivnag->output = wmiivnag_output;
			wmiivnag->scale = wmiivnag->output->scale;
			render_frame(wmiivnag);
			break;
		}
	};
}

static const struct wl_surface_listener surface_listener = {
	.enter = surface_enter,
	.leave = nop,
};

static void update_cursor(struct wmiivnag_seat *seat) {
	struct wmiivnag_pointer *pointer = &seat->pointer;
	struct wmiivnag *wmiivnag = seat->wmiivnag;
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
		cursor_theme, cursor_size * wmiivnag->scale, wmiivnag->shm);
	struct wl_cursor *cursor =
		wl_cursor_theme_get_cursor(pointer->cursor_theme, "left_ptr");
	pointer->cursor_image = cursor->images[0];
	wl_surface_set_buffer_scale(pointer->cursor_surface,
			wmiivnag->scale);
	wl_surface_attach(pointer->cursor_surface,
			wl_cursor_image_get_buffer(pointer->cursor_image), 0, 0);
	wl_pointer_set_cursor(pointer->pointer, pointer->serial,
			pointer->cursor_surface,
			pointer->cursor_image->hotspot_x / wmiivnag->scale,
			pointer->cursor_image->hotspot_y / wmiivnag->scale);
	wl_surface_damage_buffer(pointer->cursor_surface, 0, 0,
			INT32_MAX, INT32_MAX);
	wl_surface_commit(pointer->cursor_surface);
}

void update_all_cursors(struct wmiivnag *wmiivnag) {
	struct wmiivnag_seat *seat;
	wl_list_for_each(seat, &wmiivnag->seats, link) {
		if (seat->pointer.pointer) {
			update_cursor(seat);
		}
	}
}

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct wmiivnag_seat *seat = data;
	struct wmiivnag_pointer *pointer = &seat->pointer;
	pointer->x = wl_fixed_to_int(surface_x);
	pointer->y = wl_fixed_to_int(surface_y);
	pointer->serial = serial;
	update_cursor(seat);
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct wmiivnag_seat *seat = data;
	seat->pointer.x = wl_fixed_to_int(surface_x);
	seat->pointer.y = wl_fixed_to_int(surface_y);
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct wmiivnag_seat *seat = data;
	struct wmiivnag *wmiivnag = seat->wmiivnag;

	if (state != WL_POINTER_BUTTON_STATE_PRESSED) {
		return;
	}

	double x = seat->pointer.x;
	double y = seat->pointer.y;
	for (int i = 0; i < wmiivnag->buttons->length; i++) {
		struct wmiivnag_button *nagbutton = wmiivnag->buttons->items[i];
		if (x >= nagbutton->x
				&& y >= nagbutton->y
				&& x < nagbutton->x + nagbutton->width
				&& y < nagbutton->y + nagbutton->height) {
			wmiivnag_button_execute(wmiivnag, nagbutton);
			return;
		}
	}

	if (wmiivnag->details.visible &&
			wmiivnag->details.total_lines != wmiivnag->details.visible_lines) {
		struct wmiivnag_button button_up = wmiivnag->details.button_up;
		if (x >= button_up.x
				&& y >= button_up.y
				&& x < button_up.x + button_up.width
				&& y < button_up.y + button_up.height
				&& wmiivnag->details.offset > 0) {
			wmiivnag->details.offset--;
			render_frame(wmiivnag);
			return;
		}

		struct wmiivnag_button button_down = wmiivnag->details.button_down;
		int bot = wmiivnag->details.total_lines;
		bot -= wmiivnag->details.visible_lines;
		if (x >= button_down.x
				&& y >= button_down.y
				&& x < button_down.x + button_down.width
				&& y < button_down.y + button_down.height
				&& wmiivnag->details.offset < bot) {
			wmiivnag->details.offset++;
			render_frame(wmiivnag);
			return;
		}
	}
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct wmiivnag_seat *seat = data;
	struct wmiivnag *wmiivnag = seat->wmiivnag;
	if (!wmiivnag->details.visible
			|| seat->pointer.x < wmiivnag->details.x
			|| seat->pointer.y < wmiivnag->details.y
			|| seat->pointer.x >= wmiivnag->details.x + wmiivnag->details.width
			|| seat->pointer.y >= wmiivnag->details.y + wmiivnag->details.height
			|| wmiivnag->details.total_lines == wmiivnag->details.visible_lines) {
		return;
	}

	int direction = wl_fixed_to_int(value);
	int bot = wmiivnag->details.total_lines - wmiivnag->details.visible_lines;
	if (direction < 0 && wmiivnag->details.offset > 0) {
		wmiivnag->details.offset--;
	} else if (direction > 0 && wmiivnag->details.offset < bot) {
		wmiivnag->details.offset++;
	}

	render_frame(wmiivnag);
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

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct wmiivnag_seat *seat = data;
	bool cap_pointer = caps & WL_SEAT_CAPABILITY_POINTER;
	if (cap_pointer && !seat->pointer.pointer) {
		seat->pointer.pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->pointer.pointer,
				&pointer_listener, seat);
	} else if (!cap_pointer && seat->pointer.pointer) {
		wl_pointer_destroy(seat->pointer.pointer);
		seat->pointer.pointer = NULL;
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = nop,
};

static void output_scale(void *data, struct wl_output *output,
		int32_t factor) {
	struct wmiivnag_output *wmiivnag_output = data;
	wmiivnag_output->scale = factor;
	if (wmiivnag_output->wmiivnag->output == wmiivnag_output) {
		wmiivnag_output->wmiivnag->scale = wmiivnag_output->scale;
		update_all_cursors(wmiivnag_output->wmiivnag);
		render_frame(wmiivnag_output->wmiivnag);
	}
}

static void output_name(void *data, struct wl_output *output,
		const char *name) {
	struct wmiivnag_output *wmiivnag_output = data;
	wmiivnag_output->name = strdup(name);

	const char *outname = wmiivnag_output->wmiivnag->type->output;
	if (!wmiivnag_output->wmiivnag->output && outname &&
			strcmp(outname, name) == 0) {
		wmiiv_log(WMIIV_DEBUG, "Using output %s", name);
		wmiivnag_output->wmiivnag->output = wmiivnag_output;
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

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct wmiivnag *wmiivnag = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		wmiivnag->compositor = wl_registry_bind(registry, name,
				&wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wmiivnag_seat *seat =
			calloc(1, sizeof(struct wmiivnag_seat));
		if (!seat) {
			perror("calloc");
			return;
		}

		seat->wmiivnag = wmiivnag;
		seat->wl_name = name;
		seat->wl_seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);

		wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);

		wl_list_insert(&wmiivnag->seats, &seat->link);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		wmiivnag->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		if (!wmiivnag->output) {
			struct wmiivnag_output *output =
				calloc(1, sizeof(struct wmiivnag_output));
			if (!output) {
				perror("calloc");
				return;
			}
			output->wl_output = wl_registry_bind(registry, name,
					&wl_output_interface, 4);
			output->wl_name = name;
			output->scale = 1;
			output->wmiivnag = wmiivnag;
			wl_list_insert(&wmiivnag->outputs, &output->link);
			wl_output_add_listener(output->wl_output,
					&output_listener, output);
		}
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		wmiivnag->layer_shell = wl_registry_bind(
				registry, name, &zwlr_layer_shell_v1_interface, 1);
	}
}

void wmiivnag_seat_destroy(struct wmiivnag_seat *seat) {
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

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	struct wmiivnag *wmiivnag = data;
	if (wmiivnag->output->wl_name == name) {
		wmiivnag->run_display = false;
	}

	struct wmiivnag_seat *seat, *tmpseat;
	wl_list_for_each_safe(seat, tmpseat, &wmiivnag->seats, link) {
		if (seat->wl_name == name) {
			wmiivnag_seat_destroy(seat);
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

void wmiivnag_setup_cursors(struct wmiivnag *wmiivnag) {
	struct wmiivnag_seat *seat;

	wl_list_for_each(seat, &wmiivnag->seats, link) {
		struct wmiivnag_pointer *p = &seat->pointer;

		p->cursor_surface =
			wl_compositor_create_surface(wmiivnag->compositor);
		assert(p->cursor_surface);
	}
}

void wmiivnag_setup(struct wmiivnag *wmiivnag) {
	wmiivnag->display = wl_display_connect(NULL);
	if (!wmiivnag->display) {
		wmiiv_abort("Unable to connect to the compositor. "
				"If your compositor is running, check or set the "
				"WAYLAND_DISPLAY environment variable.");
	}

	wmiivnag->scale = 1;

	struct wl_registry *registry = wl_display_get_registry(wmiivnag->display);
	wl_registry_add_listener(registry, &registry_listener, wmiivnag);
	if (wl_display_roundtrip(wmiivnag->display) < 0) {
		wmiiv_abort("failed to register with the wayland display");
	}

	assert(wmiivnag->compositor && wmiivnag->layer_shell && wmiivnag->shm);

	// Second roundtrip to get wl_output properties
	if (wl_display_roundtrip(wmiivnag->display) < 0) {
		wmiiv_log(WMIIV_ERROR, "Error during outputs init.");
		wmiivnag_destroy(wmiivnag);
		exit(EXIT_FAILURE);
	}

	if (!wmiivnag->output && wmiivnag->type->output) {
		wmiiv_log(WMIIV_ERROR, "Output '%s' not found", wmiivnag->type->output);
		wmiivnag_destroy(wmiivnag);
		exit(EXIT_FAILURE);
	}

	wmiivnag_setup_cursors(wmiivnag);

	wmiivnag->surface = wl_compositor_create_surface(wmiivnag->compositor);
	assert(wmiivnag->surface);
	wl_surface_add_listener(wmiivnag->surface, &surface_listener, wmiivnag);

	wmiivnag->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			wmiivnag->layer_shell, wmiivnag->surface,
			wmiivnag->output ? wmiivnag->output->wl_output : NULL,
			wmiivnag->type->layer,
			"wmiivnag");
	assert(wmiivnag->layer_surface);
	zwlr_layer_surface_v1_add_listener(wmiivnag->layer_surface,
			&layer_surface_listener, wmiivnag);
	zwlr_layer_surface_v1_set_anchor(wmiivnag->layer_surface,
			wmiivnag->type->anchors);

	wl_registry_destroy(registry);
}

void wmiivnag_run(struct wmiivnag *wmiivnag) {
	wmiivnag->run_display = true;
	render_frame(wmiivnag);
	while (wmiivnag->run_display
			&& wl_display_dispatch(wmiivnag->display) != -1) {
		// This is intentionally left blank
	}

	if (wmiivnag->display) {
		wl_display_disconnect(wmiivnag->display);
	}
}

void wmiivnag_destroy(struct wmiivnag *wmiivnag) {
	wmiivnag->run_display = false;

	free(wmiivnag->message);
	for (int i = 0; i < wmiivnag->buttons->length; ++i) {
		struct wmiivnag_button *button = wmiivnag->buttons->items[i];
		free(button->text);
		free(button->action);
		free(button);
	}
	list_free(wmiivnag->buttons);
	free(wmiivnag->details.message);
	free(wmiivnag->details.button_up.text);
	free(wmiivnag->details.button_down.text);

	if (wmiivnag->type) {
		wmiivnag_type_free(wmiivnag->type);
	}

	if (wmiivnag->layer_surface) {
		zwlr_layer_surface_v1_destroy(wmiivnag->layer_surface);
	}

	if (wmiivnag->surface) {
		wl_surface_destroy(wmiivnag->surface);
	}

	struct wmiivnag_seat *seat, *tmpseat;
	wl_list_for_each_safe(seat, tmpseat, &wmiivnag->seats, link) {
		wmiivnag_seat_destroy(seat);
	}

	destroy_buffer(&wmiivnag->buffers[0]);
	destroy_buffer(&wmiivnag->buffers[1]);

	if (wmiivnag->outputs.prev || wmiivnag->outputs.next) {
		struct wmiivnag_output *output, *temp;
		wl_list_for_each_safe(output, temp, &wmiivnag->outputs, link) {
			wl_output_destroy(output->wl_output);
			free(output->name);
			wl_list_remove(&output->link);
			free(output);
		};
	}

	if (wmiivnag->compositor) {
		wl_compositor_destroy(wmiivnag->compositor);
	}

	if (wmiivnag->shm) {
		wl_shm_destroy(wmiivnag->shm);
	}
}
