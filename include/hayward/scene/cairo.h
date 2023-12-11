#ifndef HWD_SCENE_CAIRO_H
#define HWD_SCENE_CAIRO_H

#include <cairo.h>
#include <stddef.h>
#include <wlr/types/wlr_buffer.h>

struct wlr_buffer *
hwd_cairo_buffer_create(size_t width, size_t height);

cairo_t *
hwd_cairo_buffer_get_context(struct wlr_buffer *buffer);

#endif
