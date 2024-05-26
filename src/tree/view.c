#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/view.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <wayland-server-core.h>
#include <wayland-util.h>

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl) {
    view->type = type;
    view->impl = impl;
    wl_signal_init(&view->events.unmap);
}

void
view_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);
    assert(view->destroying);
    assert(view->window == NULL);

    wl_list_remove(&view->events.unmap.listener_list);

    if (view->impl->destroy) {
        view->impl->destroy(view);
    } else {
        free(view);
    }
}

void
view_begin_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);

    // Unmapping will mark the window as dead and trigger a transaction.  It
    // isn't safe to fully destroy the window until this transaction has
    // completed.  Setting `view->destroying` will tell the window to clean up
    // the view once it has finished cleaning up itself.
    view->destroying = true;
    if (!view->window) {
        view_destroy(view);
    }
}
