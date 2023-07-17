#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/server.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>

#include <hayward-common/log.h>

#include <hayward/config.h>
#include <hayward/desktop/idle_inhibit_v1.h>
#include <hayward/desktop/layer_shell.h>
#include <hayward/desktop/server_decoration.h>
#include <hayward/desktop/xdg_activation_v1.h>
#include <hayward/desktop/xdg_decoration.h>
#include <hayward/desktop/xdg_shell.h>
#include <hayward/desktop/xwayland.h>
#include <hayward/globals/root.h>
#include <hayward/input/input-manager.h>
#include <hayward/output.h>
#include <hayward/tree/root.h>

#include <config.h>

bool
server_privileged_prepare(struct hayward_server *server) {
    hayward_log(HAYWARD_DEBUG, "Preparing Wayland server initialization");
    server->wl_display = wl_display_create();
    server->wl_event_loop = wl_display_get_event_loop(server->wl_display);
    server->backend =
        wlr_backend_autocreate(server->wl_display, &server->session);

    if (!server->backend) {
        hayward_log(HAYWARD_ERROR, "Unable to create backend");
        return false;
    }
    return true;
}

static void
handle_drm_lease_request(struct wl_listener *listener, void *data) {
    /* We only offer non-desktop outputs, but in the future we might want to do
     * more logic here. */

    struct wlr_drm_lease_request_v1 *req = data;
    struct wlr_drm_lease_v1 *lease = wlr_drm_lease_request_v1_grant(req);
    if (!lease) {
        hayward_log(HAYWARD_ERROR, "Failed to grant lease request");
        wlr_drm_lease_request_v1_reject(req);
    }
}

bool
server_init(struct hayward_server *server) {
    hayward_log(HAYWARD_DEBUG, "Initializing Wayland server");

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        hayward_log(HAYWARD_ERROR, "Failed to create renderer");
        return false;
    }

    wlr_renderer_init_wl_shm(server->renderer, server->wl_display);

    if (wlr_renderer_get_dmabuf_texture_formats(server->renderer) != NULL) {
        wlr_drm_create(server->wl_display, server->renderer);
        server->linux_dmabuf_v1 = wlr_linux_dmabuf_v1_create_with_renderer(
            server->wl_display, 4, server->renderer
        );
    }

    server->allocator =
        wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        hayward_log(HAYWARD_ERROR, "Failed to create allocator");
        return false;
    }

    server->compositor =
        wlr_compositor_create(server->wl_display, server->renderer);
    wlr_subcompositor_create(server->wl_display);

    server->data_device_manager =
        wlr_data_device_manager_create(server->wl_display);

    wlr_gamma_control_manager_v1_create(server->wl_display);

    server->new_output.notify = handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
    server->output_layout_change.notify = handle_output_layout_change;
    wl_signal_add(
        &root->output_layout->events.change, &server->output_layout_change
    );

    wlr_xdg_output_manager_v1_create(server->wl_display, root->output_layout);

    server->idle = wlr_idle_create(server->wl_display);
    server->idle_inhibit_manager_v1 = hayward_idle_inhibit_manager_v1_create(
        server->wl_display, server->idle
    );

    server->server_decoration_manager =
        hayward_server_decoration_manager_create(server->wl_display);
    server->xdg_decoration_manager =
        hayward_xdg_decoration_manager_create(server->wl_display);

    server->layer_shell = hayward_layer_shell_create(server->wl_display);

    server->xdg_shell = hayward_xdg_shell_create(
        server->wl_display, server->server_decoration_manager
    );

    server->tablet_v2 = wlr_tablet_v2_create(server->wl_display);

    server->relative_pointer_manager =
        wlr_relative_pointer_manager_v1_create(server->wl_display);

    server->pointer_constraints =
        wlr_pointer_constraints_v1_create(server->wl_display);
    server->pointer_constraint.notify = handle_pointer_constraint;
    wl_signal_add(
        &server->pointer_constraints->events.new_constraint,
        &server->pointer_constraint
    );

    server->presentation =
        wlr_presentation_create(server->wl_display, server->backend);

    server->output_manager_v1 =
        wlr_output_manager_v1_create(server->wl_display);
    server->output_manager_apply.notify = handle_output_manager_apply;
    wl_signal_add(
        &server->output_manager_v1->events.apply, &server->output_manager_apply
    );
    server->output_manager_test.notify = handle_output_manager_test;
    wl_signal_add(
        &server->output_manager_v1->events.test, &server->output_manager_test
    );

    server->output_power_manager_v1 =
        wlr_output_power_manager_v1_create(server->wl_display);
    server->output_power_manager_set_mode.notify =
        handle_output_power_manager_set_mode;
    wl_signal_add(
        &server->output_power_manager_v1->events.set_mode,
        &server->output_power_manager_set_mode
    );
    server->input_method =
        wlr_input_method_manager_v2_create(server->wl_display);
    server->text_input = wlr_text_input_manager_v3_create(server->wl_display);
    server->foreign_toplevel_manager =
        wlr_foreign_toplevel_manager_v1_create(server->wl_display);

    hayward_session_lock_init();

    server->drm_lease_manager =
        wlr_drm_lease_v1_manager_create(server->wl_display, server->backend);
    if (server->drm_lease_manager) {
        server->drm_lease_request.notify = handle_drm_lease_request;
        wl_signal_add(
            &server->drm_lease_manager->events.request,
            &server->drm_lease_request
        );
    } else {
        hayward_log(HAYWARD_DEBUG, "Failed to create wlr_drm_lease_device_v1");
        hayward_log(HAYWARD_INFO, "VR will not be available");
    }

    wlr_export_dmabuf_manager_v1_create(server->wl_display);
    wlr_screencopy_manager_v1_create(server->wl_display);
    wlr_data_control_manager_v1_create(server->wl_display);
    wlr_primary_selection_v1_device_manager_create(server->wl_display);
    wlr_viewporter_create(server->wl_display);

    struct wlr_xdg_foreign_registry *foreign_registry =
        wlr_xdg_foreign_registry_create(server->wl_display);
    wlr_xdg_foreign_v1_create(server->wl_display, foreign_registry);
    wlr_xdg_foreign_v2_create(server->wl_display, foreign_registry);

    server->xdg_activation_v1 =
        hayward_xdg_activation_v1_create(server->wl_display);

    // Avoid using "wayland-0" as display socket
    char name_candidate[16];
    for (unsigned int i = 1; i <= 32; ++i) {
        snprintf(name_candidate, sizeof(name_candidate), "wayland-%u", i);
        if (wl_display_add_socket(server->wl_display, name_candidate) >= 0) {
            server->socket = strdup(name_candidate);
            break;
        }
    }

    if (!server->socket) {
        hayward_log(HAYWARD_ERROR, "Unable to open wayland socket");
        wlr_backend_destroy(server->backend);
        return false;
    }

    server->headless_backend = wlr_headless_backend_create(server->wl_display);
    if (!server->headless_backend) {
        hayward_log(
            HAYWARD_ERROR, "Failed to create secondary headless backend"
        );
        wlr_backend_destroy(server->backend);
        return false;
    } else {
        wlr_multi_backend_add(server->backend, server->headless_backend);
    }

    struct wlr_output *wlr_output =
        wlr_headless_add_output(server->headless_backend, 800, 600);
    wlr_output_set_name(wlr_output, "FALLBACK");
    root->fallback_output = output_create(wlr_output);
    root->pending.active_output = root->fallback_output;

    // This may have been set already via -Dtxn-timeout
    if (!server->txn_timeout_ms) {
        server->txn_timeout_ms = 200;
    }

    server->input = input_manager_create(server->wl_display, server->backend);
    input_manager_get_default_seat(); // create seat0

    return true;
}

void
server_fini(struct hayward_server *server) {
    // TODO: free hayward-specific resources
#if HAVE_XWAYLAND
    hayward_xwayland_destroy(server->xwayland);
#endif
    wl_display_destroy_clients(server->wl_display);
    wl_display_destroy(server->wl_display);
}

bool
server_start(struct hayward_server *server) {
#if HAVE_XWAYLAND
    if (config->xwayland != XWAYLAND_MODE_DISABLED) {
        hayward_log(
            HAYWARD_DEBUG, "Initializing Xwayland (lazy=%d)",
            config->xwayland == XWAYLAND_MODE_LAZY
        );
        server->xwayland = hayward_xwayland_create(
            server->wl_display, server->compositor,
            config->xwayland == XWAYLAND_MODE_LAZY
        );
        if (!server->xwayland) {
            hayward_log(HAYWARD_ERROR, "Failed to start Xwayland");
        }
    }
#endif

    hayward_log(
        HAYWARD_INFO, "Starting backend on wayland display '%s'", server->socket
    );
    if (!wlr_backend_start(server->backend)) {
        hayward_log(HAYWARD_ERROR, "Failed to start backend");
        wlr_backend_destroy(server->backend);
        return false;
    }

    return true;
}

void
server_run(struct hayward_server *server) {
    hayward_log(
        HAYWARD_INFO, "Running compositor on wayland display '%s'",
        server->socket
    );
    wl_display_run(server->wl_display);
}
