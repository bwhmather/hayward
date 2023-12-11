#include "hayward/pango.h"

#include <cairo.h>
#include <glib-object.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <stddef.h>

#include <hayward/stringop.h>

size_t
escape_markup_text(const char *src, char *dest) {
    size_t length = 0;
    if (dest) {
        dest[0] = '\0';
    }

    while (src[0]) {
        switch (src[0]) {
        case '&':
            length += 5;
            lenient_strcat(dest, "&amp;");
            break;
        case '<':
            length += 4;
            lenient_strcat(dest, "&lt;");
            break;
        case '>':
            length += 4;
            lenient_strcat(dest, "&gt;");
            break;
        case '\'':
            length += 6;
            lenient_strcat(dest, "&apos;");
            break;
        case '"':
            length += 6;
            lenient_strcat(dest, "&quot;");
            break;
        default:
            if (dest) {
                dest[length] = *src;
                dest[length + 1] = '\0';
            }
            length += 1;
        }
        src++;
    }
    return length;
}

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
