#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/transaction.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>

#include <hayward-common/log.h>

#include <hayward/desktop/idle_inhibit_v1.h>
#include <hayward/server.h>

#include <config.h>

struct hayward_transaction_manager *transaction_manager;

struct hayward_transaction_manager *
hayward_transaction_manager_create(void) {
    struct hayward_transaction_manager *transaction_manager =
        calloc(1, sizeof(struct hayward_transaction_manager));
    hayward_assert(transaction_manager != NULL, "Allocation failed");

    wl_signal_init(&transaction_manager->events.before_commit);
    wl_signal_init(&transaction_manager->events.commit);
    wl_signal_init(&transaction_manager->events.apply);
    wl_signal_init(&transaction_manager->events.after_apply);

    return transaction_manager;
}

void
hayward_transaction_manager_destroy(
    struct hayward_transaction_manager *transaction_manager
) {
    hayward_assert(transaction_manager != NULL, "Expected transaction manager");

    hayward_assert(
        wl_list_empty(&transaction_manager->events.before_commit.listener_list),
        "Manager still has registered before commit listeners"
    );
    hayward_assert(
        wl_list_empty(&transaction_manager->events.commit.listener_list),
        "Manager still has registered commit listeners"
    );
    hayward_assert(
        wl_list_empty(&transaction_manager->events.apply.listener_list),
        "Manager still has registered apply listeners"
    );
    hayward_assert(
        wl_list_empty(&transaction_manager->events.after_apply.listener_list),
        "Manager still has registered after listeners"
    );

    if (transaction_manager->timer) {
        wl_event_source_remove(transaction_manager->timer);
    }

    free(transaction_manager);
}

/**
 * Apply a transaction to the "current" state of the tree.
 */
static void
transaction_apply(struct hayward_transaction_manager *transaction_manager) {
    hayward_assert(transaction_manager != NULL, "Expected transaction manager");
    hayward_assert(
        transaction_manager->num_waiting == 0, "Can't apply while waiting"
    );

    hayward_log(HAYWARD_DEBUG, "Applying transaction");

    transaction_manager->num_configures = 0;

    transaction_manager->phase = HAYWARD_TRANSACTION_APPLY;
    wl_signal_emit_mutable(&transaction_manager->events.apply, NULL);

    transaction_manager->phase = HAYWARD_TRANSACTION_AFTER_APPLY;
    wl_signal_emit_mutable(&transaction_manager->events.after_apply, NULL);

    if (debug.txn_timings) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        struct timespec *commit = &transaction_manager->commit_time;
        float ms = (now.tv_sec - commit->tv_sec) * 1000 +
            (now.tv_nsec - commit->tv_nsec) / 1000000.0;
        hayward_log(
            HAYWARD_DEBUG, "Transaction: %.1fms waiting (%.1f frames if 60Hz)",
            ms, ms / (1000.0f / 60)
        );
    }

    if (!transaction_manager->queued) {
        hayward_idle_inhibit_v1_check_active(server.idle_inhibit_manager_v1);
    }

    transaction_manager->phase = HAYWARD_TRANSACTION_IDLE;
}

static void
transaction_progress(struct hayward_transaction_manager *transaction_manager) {
    hayward_assert(transaction_manager != NULL, "Expected transaction manager");
    hayward_assert(
        transaction_manager->phase == HAYWARD_TRANSACTION_WAITING_CONFIRM,
        "Expected transaction to be waiting for confirmations"
    );

    if (transaction_manager->num_waiting > 0) {
        return;
    }

    transaction_apply(transaction_manager);

    if (transaction_manager->queued) {
        hayward_transaction_manager_begin_transaction(transaction_manager);
        hayward_transaction_manager_end_transaction(transaction_manager);
    }
}

static int
handle_timeout(void *data) {
    struct hayward_transaction_manager *transaction_manager = data;

    hayward_log(
        HAYWARD_DEBUG, "Transaction timed out (%zi waiting)",
        transaction_manager->num_waiting
    );
    transaction_manager->num_waiting = 0;
    transaction_progress(transaction_manager);

    return 0;
}

void
hayward_transaction_manager_ensure_queued(
    struct hayward_transaction_manager *transaction_manager
) {
    transaction_manager->queued = true;
}

void
hayward_transaction_manager_begin_transaction(
    struct hayward_transaction_manager *transaction_manager
) {
    hayward_assert(
        transaction_manager->depth < 3, "Something funky is happening"
    );
    transaction_manager->depth += 1;
}

bool
hayward_transaction_manager_transaction_in_progress(
    struct hayward_transaction_manager *transaction_manager
) {
    return transaction_manager->depth > 0;
}

void
hayward_transaction_manager_end_transaction(
    struct hayward_transaction_manager *transaction_manager
) {
    hayward_assert(
        transaction_manager->depth > 0, "Transaction has not yet begun"
    );
    if (transaction_manager->depth != 1) {
        transaction_manager->depth -= 1;
        return;
    }

    if (transaction_manager->num_waiting != 0) {
        transaction_manager->depth -= 1;
        return;
    }

    if (!transaction_manager->queued) {
        transaction_manager->depth -= 1;
        return;
    }

    transaction_manager->phase = HAYWARD_TRANSACTION_BEFORE_COMMIT;
    wl_signal_emit_mutable(&transaction_manager->events.before_commit, NULL);

    transaction_manager->depth -= 1;
    transaction_manager->queued = false;

    transaction_manager->phase = HAYWARD_TRANSACTION_COMMIT;
    wl_signal_emit_mutable(&transaction_manager->events.commit, NULL);

    transaction_manager->phase = HAYWARD_TRANSACTION_WAITING_CONFIRM;

    transaction_manager->num_configures = transaction_manager->num_waiting;
    if (debug.txn_timings) {
        clock_gettime(CLOCK_MONOTONIC, &transaction_manager->commit_time);
    }
    if (debug.noatomic) {
        transaction_manager->num_waiting = 0;
    } else if (debug.txn_wait) {
        // Force the transaction to time out even if all views are ready.
        // We do this by inflating the waiting counter.
        transaction_manager->num_waiting += 1000000;
    }

    if (transaction_manager->num_waiting) {
        // Set up a timer which the views must respond within
        transaction_manager->timer = wl_event_loop_add_timer(
            server.wl_event_loop, handle_timeout, transaction_manager
        );
        if (transaction_manager->timer) {
            wl_event_source_timer_update(
                transaction_manager->timer, server.txn_timeout_ms
            );
        } else {
            hayward_log_errno(
                HAYWARD_ERROR,
                "Unable to create transaction timer "
                "(some imperfect frames might be rendered)"
            );
            transaction_manager->num_waiting = 0;
        }
    }

    transaction_progress(transaction_manager);
}

void
hayward_transaction_manager_acquire_commit_lock(
    struct hayward_transaction_manager *transaction_manager
) {
    transaction_manager->num_waiting++;
}

void
hayward_transaction_manager_release_commit_lock(
    struct hayward_transaction_manager *transaction_manager
) {
    hayward_assert(
        transaction_manager->num_waiting > 0, "No in progress transaction"
    );

    transaction_manager->num_waiting--;

    if (transaction_manager->num_waiting == 0) {
        hayward_log(HAYWARD_DEBUG, "Transaction is ready");
        wl_event_source_timer_update(transaction_manager->timer, 0);
    }

    transaction_progress(transaction_manager);
}
