#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include <config.h>

#include "hayward/tree/transaction.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include <wlr/util/log.h>

#include <hayward/profiler.h>
#include <hayward/server.h>

static void
handle_commit(void *data);

static int
handle_timeout(void *data);

struct hwd_transaction_manager *
hwd_transaction_manager_create(void) {
    struct hwd_transaction_manager *transaction_manager =
        calloc(1, sizeof(struct hwd_transaction_manager));
    assert(transaction_manager != NULL);

    wl_signal_init(&transaction_manager->events.before_commit);
    wl_signal_init(&transaction_manager->events.commit);
    wl_signal_init(&transaction_manager->events.apply);
    wl_signal_init(&transaction_manager->events.after_apply);

    return transaction_manager;
}

void
hwd_transaction_manager_destroy(struct hwd_transaction_manager *transaction_manager) {
    assert(transaction_manager != NULL);

    assert(wl_list_empty(&transaction_manager->events.before_commit.listener_list));
    assert(wl_list_empty(&transaction_manager->events.commit.listener_list));
    assert(wl_list_empty(&transaction_manager->events.apply.listener_list));
    assert(wl_list_empty(&transaction_manager->events.after_apply.listener_list));

    if (transaction_manager->timer) {
        wl_event_source_remove(transaction_manager->timer);
    }

    free(transaction_manager);
}

static void
transaction_progress(struct hwd_transaction_manager *transaction_manager) {
    assert(transaction_manager != NULL);
    assert(transaction_manager->phase == HWD_TRANSACTION_WAITING_CONFIRM);

    if (transaction_manager->num_waiting > 0) {
        return;
    }
    hwd_profiler_mark(
        "transaction confirm", transaction_manager->begin_waiting_confirm, hwd_profiler_now()
    );

    wlr_log(WLR_DEBUG, "Applying transaction");

    transaction_manager->num_configures = 0;

    transaction_manager->phase = HWD_TRANSACTION_APPLY;
    hwd_timestamp begin_apply = hwd_profiler_now();
    wl_signal_emit_mutable(&transaction_manager->events.apply, NULL);
    hwd_profiler_mark("transaction apply", begin_apply, hwd_profiler_now());

    transaction_manager->phase = HWD_TRANSACTION_AFTER_APPLY;
    hwd_timestamp begin_after_apply = hwd_profiler_now();
    wl_signal_emit_mutable(&transaction_manager->events.after_apply, NULL);
    hwd_profiler_mark("transaction after apply", begin_after_apply, hwd_profiler_now());

    hwd_profiler_mark("transaction", transaction_manager->begin_transaction, hwd_profiler_now());

    transaction_manager->phase = HWD_TRANSACTION_IDLE;

    if (transaction_manager->queued && transaction_manager->idle == NULL) {
        transaction_manager->idle =
            wl_event_loop_add_idle(server.wl_event_loop, handle_commit, transaction_manager);
    }
}

static void
handle_commit(void *data) {
    struct hwd_transaction_manager *transaction_manager = data;

    transaction_manager->idle = NULL;

    assert(transaction_manager->depth == 0);

    transaction_manager->begin_transaction = hwd_profiler_now();

    transaction_manager->phase = HWD_TRANSACTION_BEFORE_COMMIT;
    hwd_timestamp begin_before_commit = hwd_profiler_now();

    wl_signal_emit_mutable(&transaction_manager->events.before_commit, NULL);
    transaction_manager->queued = false;

    hwd_profiler_mark("transaction before commit", begin_before_commit, hwd_profiler_now());

    transaction_manager->phase = HWD_TRANSACTION_COMMIT;
    hwd_timestamp begin_commit = hwd_profiler_now();

    wl_signal_emit_mutable(&transaction_manager->events.commit, NULL);

    hwd_profiler_mark("transaction commit", begin_commit, hwd_profiler_now());

    transaction_manager->phase = HWD_TRANSACTION_WAITING_CONFIRM;
    transaction_manager->begin_waiting_confirm = hwd_profiler_now();

    transaction_manager->num_configures = transaction_manager->num_waiting;

    if (debug.noatomic) {
        transaction_manager->num_waiting = 0;
    } else if (debug.txn_wait) {
        // Force the transaction to time out even if all views are ready.
        // We do this by inflating the waiting counter.
        transaction_manager->num_waiting += 1000000;
    }

    if (transaction_manager->num_waiting) {
        // Set up a timer which the views must respond within
        transaction_manager->timer =
            wl_event_loop_add_timer(server.wl_event_loop, handle_timeout, transaction_manager);
        if (transaction_manager->timer) {
            wl_event_source_timer_update(transaction_manager->timer, server.txn_timeout_ms);
        } else {
            wlr_log_errno(
                WLR_ERROR,
                "Unable to create transaction timer "
                "(some imperfect frames might be rendered)"
            );
            transaction_manager->num_waiting = 0;
        }
    }

    transaction_progress(transaction_manager);
}

static int
handle_timeout(void *data) {
    struct hwd_transaction_manager *transaction_manager = data;

    wlr_log(WLR_DEBUG, "Transaction timed out (%zi waiting)", transaction_manager->num_waiting);
    transaction_manager->num_waiting = 0;
    transaction_progress(transaction_manager);

    return 0;
}

void
hwd_transaction_manager_ensure_queued(struct hwd_transaction_manager *transaction_manager) {
    assert(transaction_manager != NULL);

    transaction_manager->queued = true;

    if (transaction_manager->phase == HWD_TRANSACTION_IDLE && transaction_manager->idle == NULL) {
        transaction_manager->idle =
            wl_event_loop_add_idle(server.wl_event_loop, handle_commit, transaction_manager);
    }
}

void
hwd_transaction_manager_acquire_commit_lock(struct hwd_transaction_manager *transaction_manager) {
    assert(transaction_manager != NULL);
    assert(transaction_manager->phase == HWD_TRANSACTION_COMMIT);

    transaction_manager->num_waiting++;
}

void
hwd_transaction_manager_release_commit_lock(struct hwd_transaction_manager *transaction_manager) {
    assert(transaction_manager != NULL);
    assert(transaction_manager->phase == HWD_TRANSACTION_WAITING_CONFIRM);
    assert(transaction_manager->num_waiting > 0);

    transaction_manager->num_waiting--;

    if (transaction_manager->num_waiting == 0) {
        wlr_log(WLR_DEBUG, "Transaction is ready");
        wl_event_source_timer_update(transaction_manager->timer, 0);
    }

    transaction_progress(transaction_manager);
}
