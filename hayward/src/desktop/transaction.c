#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/desktop/transaction.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/desktop/idle_inhibit_v1.h>
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

#include <config.h>

static struct {
    bool queued;

    struct wl_event_source *timer;
    size_t num_waiting;
    size_t num_configures;
    struct timespec commit_time;

    struct {
        struct wl_signal transaction_commit;
        struct wl_signal transaction_apply;
    } events;
} hayward_transaction_state;

static void
validate_tree(void) {
    hayward_assert(root != NULL, "Missing root");

    // Validate that there is at least one workspace.
    struct hayward_workspace *active_workspace = root->pending.active_workspace;
    hayward_assert(active_workspace != NULL, "No active workspace");
    hayward_assert(
        list_find(root->pending.workspaces, active_workspace) != -1,
        "Active workspace missing from workspaces list"
    );

    // Validate that there is at least one output.
    struct hayward_output *active_output = root->pending.active_output;
    hayward_assert(active_output != NULL, "No active output");
    if (root->outputs->length == 0) {
        hayward_assert(
            active_output == root->fallback_output,
            "Expected fallback output to be active"
        );
    } else {
        hayward_assert(
            list_find(root->outputs, active_output) != -1,
            "Expected active output to be in outputs list"
        );
    }

    // Validate that the fallback output exists but is not in the outputs list.
    hayward_assert(root->fallback_output != NULL, "Missing fallback output");
    hayward_assert(
        list_find(root->outputs, root->fallback_output) == -1,
        "Fallback output present in outputs list"
    );

    // Validate that the correct output is focused if workspace is in tiling
    // mode.
    if (active_workspace->pending.focus_mode == F_TILING) {
        if (active_workspace->pending.active_column) {
            hayward_assert(
                active_output ==
                    active_workspace->pending.active_column->pending.output,
                "Expected active output to match active column output"
            );
        }
    }

    // Recursively validate each workspace.
    for (int i = 0; i < root->pending.workspaces->length; i++) {
        struct hayward_workspace *workspace =
            root->pending.workspaces->items[i];
        hayward_assert(workspace != NULL, "Null workspace in workspaces list");

        // Validate floating windows.
        for (int j = 0; j < workspace->pending.floating->length; j++) {
            struct hayward_window *window =
                workspace->pending.floating->items[j];
            hayward_assert(window != NULL, "NULL window in floating list");

            hayward_assert(
                window->pending.workspace == workspace,
                "Window workspace does not match expected"
            );
            hayward_assert(
                list_find(root->outputs, window->pending.output) != -1,
                "Window output missing from list"
            );
            hayward_assert(
                window->pending.parent == NULL,
                "Floating window has parent column"
            );
        }

        for (int j = 0; j < workspace->pending.tiling->length; j++) {
            struct hayward_column *column = workspace->pending.tiling->items[j];

            hayward_assert(
                column->pending.workspace == workspace,
                "Column workspace does not match expected"
            );
            hayward_assert(
                list_find(root->outputs, column->pending.output) != -1,
                "Columm output missing from list"
            );

            for (int k = 0; k < column->pending.children->length; k++) {
                struct hayward_window *window =
                    column->pending.children->items[k];

                hayward_assert(
                    window->pending.parent == column,
                    "Tiling window parent link broken"
                );
                hayward_assert(
                    window->pending.workspace == workspace,
                    "Window workspace does not match parent"
                );
                hayward_assert(
                    window->pending.output == column->pending.output,
                    "Window output does not match parent"
                );
            }
        }
    }
}

void
transaction_init(void) {
    memset(&hayward_transaction_state, 0, sizeof(hayward_transaction_state));

    wl_signal_init(&hayward_transaction_state.events.transaction_commit);
    wl_signal_init(&hayward_transaction_state.events.transaction_apply);
}

void
transaction_shutdown(void) {
    if (hayward_transaction_state.timer) {
        wl_event_source_remove(hayward_transaction_state.timer);
    }
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void
transaction_apply(void) {
    hayward_assert(
        hayward_transaction_state.num_waiting == 0, "Can't apply while waiting"
    );

    hayward_log(HAYWARD_DEBUG, "Applying transaction");

    hayward_transaction_state.num_configures = 0;

    wl_signal_emit_mutable(
        &hayward_transaction_state.events.transaction_apply, NULL
    );

    // TODO move to signal handler.
    cursor_rebase_all();

    if (debug.txn_timings) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        struct timespec *commit = &hayward_transaction_state.commit_time;
        float ms = (now.tv_sec - commit->tv_sec) * 1000 +
            (now.tv_nsec - commit->tv_nsec) / 1000000.0;
        hayward_log(
            HAYWARD_DEBUG, "Transaction: %.1fms waiting (%.1f frames if 60Hz)",
            ms, ms / (1000.0f / 60)
        );
    }

    if (!hayward_transaction_state.queued) {
        hayward_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
        return;
    }
}

static void
transaction_progress(void) {
    if (hayward_transaction_state.num_waiting > 0) {
        return;
    }

    transaction_apply();
    transaction_flush();
}

static int
handle_timeout(void *data) {
    hayward_log(
        HAYWARD_DEBUG, "Transaction timed out (%zi waiting)",
        hayward_transaction_state.num_waiting
    );
    hayward_transaction_state.num_waiting = 0;
    transaction_progress();
    return 0;
}

static void
transaction_commit(void) {
    hayward_assert(hayward_transaction_state.queued, "No transaction queued");
    hayward_assert(
        hayward_transaction_state.num_waiting == 0,
        "Can't commit when transaction is in progress"
    );
    hayward_assert(
        hayward_transaction_state.num_configures == 0,
        "Can't commit when transaction is in progress"
    );

    hayward_log(HAYWARD_DEBUG, "Committing transaction");

    hayward_transaction_state.queued = false;

#ifndef NDEBUG
    // TODO register as event listener.
    validate_tree();
#endif

    // TODO register as event listener.
    root_commit_focus();

    // TODO register as event listener.
    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seat_commit_focus(seat);
    }

    wl_signal_emit_mutable(
        &hayward_transaction_state.events.transaction_commit, NULL
    );

    hayward_transaction_state.num_configures =
        hayward_transaction_state.num_waiting;
    if (debug.txn_timings) {
        clock_gettime(CLOCK_MONOTONIC, &hayward_transaction_state.commit_time);
    }
    if (debug.noatomic) {
        hayward_transaction_state.num_waiting = 0;
    } else if (debug.txn_wait) {
        // Force the transaction to time out even if all views are ready.
        // We do this by inflating the waiting counter.
        hayward_transaction_state.num_waiting += 1000000;
    }

    if (hayward_transaction_state.num_waiting) {
        // Set up a timer which the views must respond within
        hayward_transaction_state.timer =
            wl_event_loop_add_timer(server.wl_event_loop, handle_timeout, NULL);
        if (hayward_transaction_state.timer) {
            wl_event_source_timer_update(
                hayward_transaction_state.timer, server.txn_timeout_ms
            );
        } else {
            hayward_log_errno(
                HAYWARD_ERROR,
                "Unable to create transaction timer "
                "(some imperfect frames might be rendered)"
            );
            hayward_transaction_state.num_waiting = 0;
        }
    }

    transaction_progress();
}

void
transaction_notify_view_ready_by_serial(
    struct hayward_view *view, uint32_t serial
) {
    struct hayward_window *window = view->window;

    if (!window->is_configuring) {
        return;
    }
    if (window->configure_serial == 0) {
        return;
    }
    if (serial != window->configure_serial) {
        return;
    }

    transaction_release();
}

void
transaction_notify_view_ready_by_geometry(
    struct hayward_view *view, double x, double y, int width, int height
) {
    struct hayward_window *window = view->window;
    struct hayward_window_state *state = &window->committed;

    if (!window->is_configuring) {
        return;
    }
    if (window->configure_serial != 0) {
        return;
    }

    if ((int)state->content_x != (int)x || (int)state->content_y != (int)y ||
        state->content_width != width || state->content_height != height) {
        return;
    }
    transaction_release();
}

void
transaction_add_commit_listener(struct wl_listener *listener) {
    wl_signal_add(
        &hayward_transaction_state.events.transaction_commit, listener
    );
}

void
transaction_add_apply_listener(struct wl_listener *listener) {
    wl_signal_add(
        &hayward_transaction_state.events.transaction_apply, listener
    );
}

void
transaction_ensure_queued(void) {
    hayward_transaction_state.queued = true;
}

void
transaction_acquire(void) {
    hayward_transaction_state.num_waiting++;
}

void
transaction_release(void) {
    hayward_assert(
        hayward_transaction_state.num_waiting > 0, "No in progress transaction"
    );

    hayward_transaction_state.num_waiting--;

    if (hayward_transaction_state.num_waiting == 0) {
        hayward_log(HAYWARD_DEBUG, "Transaction is ready");
        wl_event_source_timer_update(hayward_transaction_state.timer, 0);
    }

    transaction_progress();
}

void
transaction_flush(void) {
    if (hayward_transaction_state.num_waiting != 0) {
        return;
    }

    if (!hayward_transaction_state.queued) {
        return;
    }

    transaction_commit();
}
