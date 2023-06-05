#ifndef _HAYWARDNAG_TYPES_H
#define _HAYWARDNAG_TYPES_H

#include <pango/pango.h>
#include <stdint.h>
#include <sys/types.h>

#include <hayward-common/list.h>

struct haywardnag_type {
    char *name;

    PangoFontDescription *font_description;
    char *output;
    uint32_t anchors;
    int32_t layer; // enum zwlr_layer_shell_v1_layer or -1 if unset

    // Colors
    uint32_t button_text;
    uint32_t button_background;
    uint32_t details_background;
    uint32_t background;
    uint32_t text;
    uint32_t border;
    uint32_t border_bottom;

    // Sizing
    ssize_t bar_border_thickness;
    ssize_t message_padding;
    ssize_t details_border_thickness;
    ssize_t button_border_thickness;
    ssize_t button_gap;
    ssize_t button_gap_close;
    ssize_t button_margin_right;
    ssize_t button_padding;
};

struct haywardnag_type *
haywardnag_type_new(const char *name);

void
haywardnag_types_add_default(list_t *types);

struct haywardnag_type *
haywardnag_type_get(list_t *types, char *name);

struct haywardnag_type *
haywardnag_type_clone(struct haywardnag_type *type);

void
haywardnag_type_merge(
    struct haywardnag_type *dest, struct haywardnag_type *src
);

void
haywardnag_type_free(struct haywardnag_type *type);

void
haywardnag_types_free(list_t *types);

#endif
