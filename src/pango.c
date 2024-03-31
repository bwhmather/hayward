#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/pango.h"

#include <cairo.h>
#include <glib-object.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

void
get_text_metrics(const PangoFontDescription *description, int *height, int *baseline) {
    cairo_t *cairo = cairo_create(NULL);
    PangoContext *pango = pango_cairo_create_context(cairo);
    // When passing NULL as a language, pango uses the current locale.
    PangoFontMetrics *metrics = pango_context_get_metrics(pango, description, NULL);

    *baseline = pango_font_metrics_get_ascent(metrics) / PANGO_SCALE;
    *height = *baseline + pango_font_metrics_get_descent(metrics) / PANGO_SCALE;

    pango_font_metrics_unref(metrics);
    g_object_unref(pango);
    cairo_destroy(cairo);
}
