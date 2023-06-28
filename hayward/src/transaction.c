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

static struct {
    int depth;
    bool queued;

    struct wl_event_source *timer;
    size_t num_waiting;
    size_t num_configures;
    struct timespec commit_time;

    struct {
        struct wl_signal transaction_before_commit;
        struct wl_signal transaction_commit;
        struct wl_signal transaction_apply;
        struct wl_signal transaction_after_apply;
    } events;
} hayward_transaction_state;

void
transaction_init(void) {
    memset(&hayward_transaction_state, 0, sizeof(hayward_transaction_state));

    wl_signal_init(&hayward_transaction_state.events.transaction_before_commit);
    wl_signal_init(&hayward_transaction_state.events.transaction_commit);
    wl_signal_init(&hayward_transaction_state.events.transaction_apply);
    wl_signal_init(&hayward_transaction_state.events.transaction_after_apply);
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

    wl_signal_emit_mutable(
        &hayward_transaction_state.events.transaction_after_apply, NULL
    );

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

    if (hayward_transaction_state.queued) {
        transaction_begin();
        transaction_flush();
    }
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

void
transaction_add_before_commit_listener(struct wl_listener *listener) {
    wl_signal_add(
        &hayward_transaction_state.events.transaction_before_commit, listener
    );
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
transaction_add_after_apply_listener(struct wl_listener *listener) {
    wl_signal_add(
        &hayward_transaction_state.events.transaction_after_apply, listener
    );
}

void
transaction_ensure_queued(void) {
    hayward_transaction_state.queued = true;
}

void
transaction_begin_(void) {
    hayward_assert(
        hayward_transaction_state.depth < 3, "Something funky is happening"
    );
    hayward_transaction_state.depth += 1;
}

bool
transaction_in_progress(void) {
    return hayward_transaction_state.depth > 0;
}

void
transaction_flush_(void) {
    hayward_assert(
        hayward_transaction_state.depth > 0, "Transaction has not yet begun"
    );
    if (hayward_transaction_state.depth != 1) {
        hayward_transaction_state.depth -= 1;
        return;
    }

    if (hayward_transaction_state.num_waiting != 0) {
        hayward_transaction_state.depth -= 1;
        return;
    }

    if (!hayward_transaction_state.queued) {
        hayward_transaction_state.depth -= 1;
        return;
    }

    wl_signal_emit_mutable(
        &hayward_transaction_state.events.transaction_before_commit, NULL
    );

    hayward_transaction_state.depth -= 1;
    hayward_transaction_state.queued = false;

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
