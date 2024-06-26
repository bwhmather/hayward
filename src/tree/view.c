#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/tree/view.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl) {
    view->type = type;
    view->impl = impl;
}

void
view_destroy(struct hwd_view *view) {
    assert(view->surface == NULL);
    assert(view->destroying);

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
}
