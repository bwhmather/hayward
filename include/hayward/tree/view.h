#ifndef HWD_TREE_VIEW_H
#define HWD_TREE_VIEW_H

#include <config.h>

#include <stdbool.h>

#include <wlr/types/wlr_compositor.h>

struct hwd_window;
struct hwd_view;

enum hwd_view_type {
    HWD_VIEW_XDG_SHELL,
#if HAVE_XWAYLAND
    HWD_VIEW_XWAYLAND,
#endif
};

struct hwd_view_impl {
    void (*set_activated)(struct hwd_view *view, bool activated);
    void (*close_popups)(struct hwd_view *view);
    void (*destroy)(struct hwd_view *view);
};

struct hwd_view {
    enum hwd_view_type type;
    const struct hwd_view_impl *impl;

    struct wlr_surface *surface; // NULL for unmapped views

    bool destroying;
};

void
view_init(struct hwd_view *view, enum hwd_view_type type, const struct hwd_view_impl *impl);

void
view_destroy(struct hwd_view *view);

void
view_begin_destroy(struct hwd_view *view);

#endif
