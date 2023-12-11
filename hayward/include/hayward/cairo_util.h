#ifndef _HWD_CAIRO_UTIL_H
#define _HWD_CAIRO_UTIL_H
#include <config.h>

#include <cairo.h>
#include <stdint.h>

#include <wayland-client-protocol.h>

cairo_subpixel_order_t
to_cairo_subpixel_order(enum wl_output_subpixel subpixel);

#endif
