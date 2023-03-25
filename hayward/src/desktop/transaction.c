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
#include <hayward/tree/node.h>
#include <hayward/tree/root.h>
#include <hayward/tree/view.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

static struct {
    struct {
        struct wl_signal transaction_commit;
        struct wl_signal transaction_apply;
    } events;
} hayward_transaction_state;

void
transaction_init(void) {
    wl_signal_init(&hayward_transaction_state.events.transaction_commit);
    wl_signal_init(&hayward_transaction_state.events.transaction_apply);
}

void
transaction_shutdown(void) {}

struct hayward_transaction {
    struct wl_event_source *timer;
    list_t *instructions; // struct hayward_transaction_instruction *
    size_t num_waiting;
    size_t num_configures;
    struct timespec commit_time;
};

struct hayward_transaction_instruction {
    struct hayward_transaction *transaction;
    struct hayward_node *node;
    uint32_t serial;
    bool server_request;
    bool waiting;
};

static struct hayward_transaction *
transaction_create(void) {
    struct hayward_transaction *transaction =
        calloc(1, sizeof(struct hayward_transaction));
    hayward_assert(transaction, "Unable to allocate transaction");

    transaction->instructions = create_list();
    return transaction;
}

static void
transaction_destroy(struct hayward_transaction *transaction) {
    // Free instructions
    for (int i = 0; i < transaction->instructions->length; ++i) {
        struct hayward_transaction_instruction *instruction =
            transaction->instructions->items[i];
        struct hayward_node *node = instruction->node;
        node->ntxnrefs--;
        if (node->instruction == instruction) {
            node->instruction = NULL;
        }
        if (node->destroying && node->ntxnrefs == 0) {
            switch (node->type) {
            case N_ROOT:
                hayward_abort("Never reached");
            case N_OUTPUT:
                output_destroy(node->hayward_output);
                break;
            case N_WORKSPACE:
                hayward_assert(false, "workspaces now handled using events");
                break;
            case N_COLUMN:
                hayward_assert(false, "columns now handled using events");
                break;
            case N_WINDOW:
                hayward_assert(false, "windows now handled using events");
                break;
            }
        }
        free(instruction);
    }
    list_free(transaction->instructions);

    if (transaction->timer) {
        wl_event_source_remove(transaction->timer);
    }
    free(transaction);
}

static void
copy_output_state(
    struct hayward_output *output,
    struct hayward_transaction_instruction *instruction
) {}

static void
transaction_add_node(
    struct hayward_transaction *transaction, struct hayward_node *node,
    bool server_request
) {
    struct hayward_transaction_instruction *instruction = NULL;

    // Check if we have an instruction for this node already, in which case we
    // update that instead of creating a new one.
    if (node->ntxnrefs > 0) {
        for (int idx = 0; idx < transaction->instructions->length; idx++) {
            struct hayward_transaction_instruction *other =
                transaction->instructions->items[idx];
            if (other->node == node) {
                instruction = other;
                break;
            }
        }
    }

    if (!instruction) {
        instruction = calloc(1, sizeof(struct hayward_transaction_instruction));
        hayward_assert(instruction, "Unable to allocate instruction");

        instruction->transaction = transaction;
        instruction->node = node;
        instruction->server_request = server_request;

        list_add(transaction->instructions, instruction);
        node->ntxnrefs++;
    } else if (server_request) {
        instruction->server_request = true;
    }

    switch (node->type) {
    case N_ROOT:
        hayward_assert(false, "root now handled using events");
        break;
    case N_OUTPUT:
        copy_output_state(node->hayward_output, instruction);
        break;
    case N_WORKSPACE:
        hayward_assert(false, "workspaces now handled using events");
        break;
    case N_COLUMN:
        hayward_assert(false, "columns now handled using events");
        break;
    case N_WINDOW:
        hayward_assert(false, "windows now handled using events");
        break;
    }
}

static void
apply_output_state(
    struct hayward_output *output, struct hayward_output_state *state
) {
    output_damage_whole(output);
    memcpy(&output->current, state, sizeof(struct hayward_output_state));
    output_damage_whole(output);
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void
transaction_apply(struct hayward_transaction *transaction) {
    hayward_log(HAYWARD_DEBUG, "Applying transaction %p", (void *)transaction);
    if (debug.txn_timings) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        struct timespec *commit = &transaction->commit_time;
        float ms = (now.tv_sec - commit->tv_sec) * 1000 +
            (now.tv_nsec - commit->tv_nsec) / 1000000.0;
        hayward_log(
            HAYWARD_DEBUG,
            "Transaction %p: %.1fms waiting "
            "(%.1f frames if 60Hz)",
            (void *)transaction, ms, ms / (1000.0f / 60)
        );
    }

    // Apply the instruction state to the node's current state
    for (int i = 0; i < transaction->instructions->length; ++i) {
        struct hayward_transaction_instruction *instruction =
            transaction->instructions->items[i];
        struct hayward_node *node = instruction->node;

        switch (node->type) {
        case N_ROOT:
            hayward_assert(false, "root now handled using events");
            break;
        case N_OUTPUT:
            apply_output_state(
                node->hayward_output,
                &instruction->node->hayward_output->committed
            );
            break;
        case N_WORKSPACE:
            hayward_assert(false, "workspaces now handled using events");
            break;
        case N_COLUMN:
            hayward_assert(false, "columns now handled using events");
            break;
        case N_WINDOW:
            hayward_assert(false, "windows now handled using events");
            break;
        }

        node->instruction = NULL;
    }

    cursor_rebase_all();
}

static void
transaction_commit_pending(void);

static void
transaction_progress(void) {
    if (!server.queued_transaction) {
        return;
    }
    if (server.queued_transaction->num_waiting > 0) {
        return;
    }
    wl_signal_emit_mutable(
        &hayward_transaction_state.events.transaction_apply, NULL
    );
    transaction_apply(server.queued_transaction);
    transaction_destroy(server.queued_transaction);
    server.queued_transaction = NULL;

    if (!server.pending_transaction) {
        hayward_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
        return;
    }

    transaction_commit_pending();
}

static int
handle_timeout(void *data) {
    struct hayward_transaction *transaction = data;
    hayward_log(
        HAYWARD_DEBUG, "Transaction %p timed out (%zi waiting)",
        (void *)transaction, transaction->num_waiting
    );
    transaction->num_waiting = 0;
    transaction_progress();
    return 0;
}

static void
transaction_commit(struct hayward_transaction *transaction) {
    hayward_log(
        HAYWARD_DEBUG, "Transaction %p committing with %i instructions",
        (void *)transaction, transaction->instructions->length
    );
    transaction->num_waiting = 0;

    wl_signal_emit_mutable(
        &hayward_transaction_state.events.transaction_commit, NULL
    );

    for (int i = 0; i < transaction->instructions->length; ++i) {
        struct hayward_transaction_instruction *instruction =
            transaction->instructions->items[i];
        struct hayward_node *node = instruction->node;
        node->instruction = instruction;
    }

    transaction->num_configures = transaction->num_waiting;
    if (debug.txn_timings) {
        clock_gettime(CLOCK_MONOTONIC, &transaction->commit_time);
    }
    if (debug.noatomic) {
        transaction->num_waiting = 0;
    } else if (debug.txn_wait) {
        // Force the transaction to time out even if all views are ready.
        // We do this by inflating the waiting counter.
        transaction->num_waiting += 1000000;
    }

    if (transaction->num_waiting) {
        // Set up a timer which the views must respond within
        transaction->timer = wl_event_loop_add_timer(
            server.wl_event_loop, handle_timeout, transaction
        );
        if (transaction->timer) {
            wl_event_source_timer_update(
                transaction->timer, server.txn_timeout_ms
            );
        } else {
            hayward_log_errno(
                HAYWARD_ERROR,
                "Unable to create transaction timer "
                "(some imperfect frames might be rendered)"
            );
            transaction->num_waiting = 0;
        }
    }
}

static void
transaction_commit_pending(void) {
    if (server.queued_transaction) {
        return;
    }
    struct hayward_transaction *transaction = server.pending_transaction;
    server.pending_transaction = NULL;
    server.queued_transaction = transaction;

    transaction_commit(transaction);
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
transaction_acquire(void) {
    struct hayward_transaction *transaction = server.queued_transaction;
    hayward_assert(transaction != NULL, "No in progress transaction");

    transaction->num_waiting++;
}

void
transaction_release(void) {
    struct hayward_transaction *transaction = server.queued_transaction;
    hayward_assert(transaction != NULL, "No in progress transaction");
    hayward_assert(
        transaction->num_waiting > 0, "No active locks on transaction"
    );

    transaction->num_waiting--;

    if (transaction->num_waiting == 0) {
        hayward_log(
            HAYWARD_DEBUG, "Transaction %p is ready", (void *)transaction
        );
        wl_event_source_timer_update(transaction->timer, 0);
    }

    transaction_progress();
}

static void
_transaction_commit_dirty(bool server_request) {
#ifndef NDEBUG
    validate_tree();
#endif

    root_commit_focus();

    struct hayward_seat *seat;
    wl_list_for_each(seat, &server.input->seats, link) {
        seat_commit_focus(seat);
    }

    if (!server.pending_transaction) {
        server.pending_transaction = transaction_create();
        if (!server.pending_transaction) {
            return;
        }
    }

    for (int i = 0; i < server.dirty_nodes->length; ++i) {
        struct hayward_node *node = server.dirty_nodes->items[i];
        transaction_add_node(server.pending_transaction, node, server_request);
        node->dirty = false;
    }
    server.dirty_nodes->length = 0;

    transaction_commit_pending();
}

void
transaction_commit_dirty(void) {
    _transaction_commit_dirty(true);
}

void
transaction_commit_dirty_client(void) {
    _transaction_commit_dirty(false);
}
