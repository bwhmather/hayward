#ifndef _HAYWARD_TRANSACTION_H
#define _HAYWARD_TRANSACTION_H
#include <stdint.h>
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
 * in containers, mark them as dirty and call transaction_commit_dirty(). This
 * create and commits a transaction from the dirty containers.
 */

struct hayward_view;

void
transaction_init(void);

void
transaction_shutdown(void);

/**
 * Notify the transaction system that a view is ready for the new layout.
 *
 * When all views in the transaction are ready, the layout will be applied.
 */
void
transaction_notify_view_ready_by_serial(
    struct hayward_view *view, uint32_t serial
);

/**
 * Notify the transaction system that a view is ready for the new layout, but
 * identifying the instruction by geometry rather than by serial.
 *
 * This is used by xwayland views, as they don't have serials.
 */
void
transaction_notify_view_ready_by_geometry(
    struct hayward_view *view, double x, double y, int width, int height
);

void
transaction_add_commit_listener(struct wl_listener *listener);

void
transaction_add_apply_listener(struct wl_listener *listener);

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

/**
 * Find all dirty containers, create and commit a transaction containing them,
 * and unmark them as dirty.
 */
void
transaction_commit_dirty(void);

/*
 * Same as transaction_commit_dirty, but signalling that this is a
 * client-initiated change has already taken effect.
 */
void
transaction_commit_dirty_client(void);

#endif
