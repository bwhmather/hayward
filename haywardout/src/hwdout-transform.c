#include "hwdout-transform.h"

#include <glib-object.h>

GType
hwdout_transform_get_type(void) {
    static GType the_type = 0;

    if (the_type == 0) {
        static const GEnumValue values[] = {
            {HWDOUT_TRANSFORM_NORMAL, "HWDOUT_TRANSFORM_NORMAL", "normal"},
            {HWDOUT_TRANSFORM_90, "HWDOUT_TRANSFORM_90", "90"},
            {HWDOUT_TRANSFORM_180, "HWDOUT_TRANSFORM_180", "180"},
            {HWDOUT_TRANSFORM_270, "HWDOUT_TRANSFORM_270", "270"},
            {HWDOUT_TRANSFORM_FLIPPED, "HWDOUT_TRANSFORM_FLIPPED", "flipped"},
            {HWDOUT_TRANSFORM_FLIPPED_90, "HWDOUT_TRANSFORM_FLIPPED_90", "flipped-90"},
            {HWDOUT_TRANSFORM_FLIPPED_180, "HWDOUT_TRANSFORM_FLIPPED_180", "flipped-180"},
            {HWDOUT_TRANSFORM_FLIPPED_270, "HWDOUT_TRANSFORM_FLIPPED_270", "flipped-270"},
            {0, NULL, NULL}};
        the_type = g_enum_register_static(g_intern_static_string("HwdoutTransform"), values);
    }
    return the_type;
}
