#ifndef HWD_PANGO_H
#define HWD_PANGO_H

#include <pango/pango.h>
#include <stddef.h>

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
