#ifndef _HWD_PANGO_H
#define _HWD_PANGO_H
#include <cairo.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include <hayward/stringop.h>
/**
 * Utility function which escape characters a & < > ' ".
 *
 * The function returns the length of the escaped string, optionally writing the
 * escaped string to dest if provided.
 */
size_t
escape_markup_text(const char *src, char *dest);
void
get_text_metrics(const PangoFontDescription *desc, int *height, int *baseline);

#endif
