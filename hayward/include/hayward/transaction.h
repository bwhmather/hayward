#ifndef HAYWARD_TRANSACTION_H
#define HAYWARD_TRANSACTION_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>

#include <hayward-common/log.h>

#include <hayward/desktop/idle_inhibit_v1.h>
#include <hayward/server.h>

#include <config.h>

/**
 * Transactions enable us to perform atomic layout updates.
 *
 * A transaction contains a list of containers and their new state.
 * A state might contain a new size, or new border settings, or new parent/child
 * relationships.
 *
 * Committing a transaction makes Hayward notify of all the affected clients
 * with their new sizes. We then wait for all the views to respond with their
 * new surface sizes. When all are ready, or when a timeout has passed, we apply
 * the updates all at the same time.
 *
 * When we want to make adjustments to the layout, we change the pending state
 * in containers, mark them as dirty and call transaction_end(). This
 * create and commits a transaction from the dirty containers.
 */

struct hayward_transaction_manager {
    int depth;
    bool queued;

    struct wl_event_source *timer;
    size_t num_waiting;
    size_t num_configures;
    struct timespec commit_time;

    struct {
        struct wl_signal before_commit;
        struct wl_signal commit;
        struct wl_signal apply;
        struct wl_signal after_apply;
    } events;
};

struct hayward_transaction_manager *
hayward_transaction_manager_create();

void
hayward_transaction_manager_destroy(struct hayward_transaction_manager *manager
);

void
hayward_transaction_manager_ensure_queued(
    struct hayward_transaction_manager *manager
);

void
hayward_transaction_manager_begin_transaction(
    struct hayward_transaction_manager *manager
);

bool
hayward_transaction_manager_transaction_in_progress(
    struct hayward_transaction_manager *manager
);

void
hayward_transaction_manager_end_transaction(
    struct hayward_transaction_manager *manager
);

/**
 * Can be called during handling of a commit event to inform the transaction
 * of work that needs to be done.  Once the work is done, the lock should be
 * released.  Used by views to block the transaction once asked to reconfigure.
 * Transactions can time out, in which eventuality the apply event will be
 * triggered and all locks should be forgotten.
 */
void
hayward_transaction_manager_acquire_commit_lock(
    struct hayward_transaction_manager *manager
);

void
hayward_transaction_manager_release_commit_lock(
    struct hayward_transaction_manager *manager
);

#endif
