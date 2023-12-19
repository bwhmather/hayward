#ifndef HWD_PANGO_H
#define HWD_PANGO_H

#include <pango/pango.h>

void
get_text_metrics(const PangoFontDescription *desc, int *height, int *baseline);

#endif
