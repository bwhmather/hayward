#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/desktop/wlr_output_management_v1.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

static void
output_manager_update_config(struct hwd_wlr_output_manager_v1 *manager) {
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();

    struct wlr_output_layout_output *layout_output;
    wl_list_for_each(layout_output, &manager->output_layout->outputs, link) {
        struct wlr_output *output = layout_output->output;

        struct wlr_output_configuration_head_v1 *config_head =
            wlr_output_configuration_head_v1_create(config, output);

        struct wlr_box output_box;
        wlr_output_layout_get_box(manager->output_layout, output, &output_box);
        if (!wlr_box_empty(&output_box)) {
            config_head->state.x = output_box.x;
            config_head->state.y = output_box.y;
        }
    }

    wlr_output_manager_v1_set_configuration(manager->wlr_manager, config);
}

static void
output_manager_apply(
    struct hwd_wlr_output_manager_v1 *manager, struct wlr_output_configuration_v1 *config,
    bool test_only
) {
    bool ok = true;

    // Disable outputs first to conserve CRTs.
    struct wlr_output_configuration_head_v1 *config_head;
    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output *wlr_output = config_head->state.output;
        if (config_head->state.enabled) {
            continue;
        }

        struct wlr_output_state new_state;
        wlr_output_state_init(&new_state);

        wlr_output_state_set_enabled(&new_state, false);

        if (test_only) {
            if (!wlr_output_test_state(wlr_output, &new_state)) {
                ok = false;
            }
        } else {
            if (!wlr_output_commit_state(wlr_output, &new_state)) {
                wlr_log(WLR_ERROR, "Failed to commit output %s", wlr_output->name);
                ok = false;
            }
        }

        wlr_output_state_finish(&new_state);
    }

    // Then enable outputs that need to
    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output *wlr_output = config_head->state.output;

        if (!config_head->state.enabled) {
            continue;
        }

        struct wlr_output_state new_state;
        wlr_output_state_init(&new_state);

        wlr_output_state_set_enabled(&new_state, true);

        if (config_head->state.mode != NULL) {
            wlr_output_state_set_mode(&new_state, config_head->state.mode);
        } else {
            wlr_output_state_set_custom_mode(
                &new_state, config_head->state.custom_mode.width,
                config_head->state.custom_mode.height, config_head->state.custom_mode.refresh
            );
        }

        wlr_output_state_set_transform(&new_state, config_head->state.transform);
        wlr_output_state_set_scale(&new_state, config_head->state.scale);

        if (test_only) {
            if (!wlr_output_test_state(wlr_output, &new_state)) {
                ok = false;
            }
        } else {
            if (!wlr_output_commit_state(wlr_output, &new_state)) {
                wlr_log(WLR_ERROR, "Failed to commit output %s", wlr_output->name);
                ok = false;
            }
        }

        wlr_output_state_finish(&new_state);

        wlr_output_layout_add(
            manager->output_layout, wlr_output, config_head->state.x, config_head->state.y
        );
    }

    if (ok) {
        wlr_output_configuration_v1_send_succeeded(config);
    } else {
        wlr_output_configuration_v1_send_failed(config);
    }
    wlr_output_configuration_v1_destroy(config);
}

static void
handle_output_manager_apply(struct wl_listener *listener, void *data) {
    struct hwd_wlr_output_manager_v1 *manager =
        wl_container_of(listener, manager, output_manager_apply);
    struct wlr_output_configuration_v1 *config = data;

    output_manager_apply(manager, config, false);
}

static void
handle_output_manager_test(struct wl_listener *listener, void *data) {
    struct hwd_wlr_output_manager_v1 *manager =
        wl_container_of(listener, manager, output_manager_test);
    struct wlr_output_configuration_v1 *config = data;

    output_manager_apply(manager, config, true);
}

static void
handle_output_layout_change(struct wl_listener *listener, void *data) {
    struct hwd_wlr_output_manager_v1 *manager =
        wl_container_of(listener, manager, output_layout_change);
    output_manager_update_config(manager);
}

static void
handle_output_manager_destroy(struct wl_listener *listener, void *data) {
    struct hwd_wlr_output_manager_v1 *manager =
        wl_container_of(listener, manager, output_layout_destroy);

    wl_list_remove(&manager->output_manager_apply.link);
    wl_list_remove(&manager->output_manager_test.link);
    wl_list_remove(&manager->output_manager_destroy.link);
    wl_list_remove(&manager->output_layout_change.link);
    wl_list_remove(&manager->output_layout_destroy.link);

    wl_signal_emit_mutable(&manager->events.destroy, manager);

    free(manager);
}

static void
handle_output_layout_destroy(struct wl_listener *listener, void *data) {
    struct hwd_wlr_output_manager_v1 *manager =
        wl_container_of(listener, manager, output_layout_destroy);

    wl_list_remove(&manager->output_manager_apply.link);
    wl_list_remove(&manager->output_manager_test.link);
    wl_list_remove(&manager->output_manager_destroy.link);
    wl_list_remove(&manager->output_layout_change.link);
    wl_list_remove(&manager->output_layout_destroy.link);
}

struct hwd_wlr_output_manager_v1 *
hwd_wlr_output_manager_v1_create(
    struct wl_display *wl_display, struct wlr_output_layout *output_layout
) {
    struct hwd_wlr_output_manager_v1 *manager = calloc(1, sizeof(struct hwd_wlr_output_manager_v1));
    assert(manager != NULL);

    manager->wlr_manager = wlr_output_manager_v1_create(wl_display);
    assert(manager->wlr_manager != NULL);
    manager->output_manager_apply.notify = handle_output_manager_apply;
    wl_signal_add(&manager->wlr_manager->events.apply, &manager->output_manager_apply);
    manager->output_manager_test.notify = handle_output_manager_test;
    wl_signal_add(&manager->wlr_manager->events.test, &manager->output_manager_test);
    manager->output_manager_destroy.notify = handle_output_manager_destroy;
    wl_signal_add(&manager->wlr_manager->events.destroy, &manager->output_manager_destroy);

    manager->output_layout = output_layout;
    manager->output_layout_change.notify = handle_output_layout_change;
    wl_signal_add(&manager->output_layout->events.change, &manager->output_layout_change);
    manager->output_layout_destroy.notify = handle_output_layout_destroy;
    wl_signal_add(&manager->output_layout->events.destroy, &manager->output_layout_destroy);

    wl_signal_init(&manager->events.destroy);

    return manager;
}
