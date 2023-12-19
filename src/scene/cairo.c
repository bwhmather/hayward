#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "hayward/scene/cairo.h"

#include <cairo.h>
#include <drm_fourcc.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <wayland-util.h>

#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_buffer.h>

struct hwd_cairo_buffer {
    struct wlr_buffer base;
    cairo_surface_t *surface;
    cairo_t *cairo;
};

static void
hwd_cairo_buffer_handle_destroy(struct wlr_buffer *wlr_buffer) {
    struct hwd_cairo_buffer *cairo_buffer = wl_container_of(wlr_buffer, cairo_buffer, base);

    cairo_surface_destroy(cairo_buffer->surface);
    cairo_destroy(cairo_buffer->cairo);
    free(cairo_buffer);
}

static bool
hwd_cairo_buffer_handle_begin_data_ptr_access(
    struct wlr_buffer *wlr_buffer, uint32_t flags, void **data, uint32_t *format, size_t *stride
) {
    struct hwd_cairo_buffer *cairo_buffer = wl_container_of(wlr_buffer, cairo_buffer, base);

    *data = cairo_image_surface_get_data(cairo_buffer->surface);
    *stride = cairo_image_surface_get_stride(cairo_buffer->surface);
    *format = DRM_FORMAT_ARGB8888;
    return true;
}

static void
hwd_cairo_buffer_handle_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    // This space is intentionally left blank
}

static const struct wlr_buffer_impl hwd_cairo_buffer_impl = {
    .destroy = hwd_cairo_buffer_handle_destroy,
    .begin_data_ptr_access = hwd_cairo_buffer_handle_begin_data_ptr_access,
    .end_data_ptr_access = hwd_cairo_buffer_handle_end_data_ptr_access,
};

struct wlr_buffer *
hwd_cairo_buffer_create(size_t width, size_t height) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (surface == NULL) {
        return NULL;
    }

    cairo_t *cairo = cairo_create(surface);
    if (cairo == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

    struct hwd_cairo_buffer *cairo_buffer = calloc(1, sizeof(struct hwd_cairo_buffer));
    if (cairo_buffer == NULL) {
        cairo_destroy(cairo);
        cairo_surface_destroy(surface);
        return NULL;
    }
    wlr_buffer_init(&cairo_buffer->base, &hwd_cairo_buffer_impl, width, height);
    cairo_buffer->surface = surface;
    cairo_buffer->cairo = cairo;

    return &cairo_buffer->base;
}

cairo_t *
hwd_cairo_buffer_get_context(struct wlr_buffer *wlr_buffer) {
    struct hwd_cairo_buffer *cairo_buffer = wl_container_of(wlr_buffer, cairo_buffer, base);

    return cairo_buffer->cairo;
}
