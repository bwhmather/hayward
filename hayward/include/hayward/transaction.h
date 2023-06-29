#ifndef HAYWARD_TRANSACTION_H
#define HAYWARD_TRANSACTION_H

#include <wayland-server-core.h>

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

struct hayward_view;

void
transaction_init(void);

void
transaction_shutdown(void);

void
transaction_add_before_commit_listener(struct wl_listener *listener);

void
transaction_add_commit_listener(struct wl_listener *listener);

void
transaction_add_apply_listener(struct wl_listener *listener);

void
transaction_add_after_apply_listener(struct wl_listener *listener);

void
transaction_ensure_queued(void);

void
transaction_begin(void);

bool
transaction_in_progress(void);

void
transaction_end(void);

/**
 * Can be called during handling of a commit event to inform the transaction
 * of work that needs to be done.  Once the work is done, the lock should be
 * released.  Used by views to block the transaction once asked to reconfigure.
 * Transactions can time out, in which eventuality the apply event will be
 * triggered and all locks should be forgotten.
 */
void
transaction_acquire(void);

void
transaction_release(void);

#endif
