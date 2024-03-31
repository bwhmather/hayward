#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/scene/colours.h"

#include <assert.h>

struct hwd_colour
hwd_lighten(float s, struct hwd_colour in) {
    assert(s >= 0.0 && s <= 1.0);
    struct hwd_colour out = {
        1 - (1 - s) * (1 - in.r),
        1 - (1 - s) * (1 - in.g),
        1 - (1 - s) * (1 - in.b),
        in.a,
    };
    return out;
}

struct hwd_colour
hwd_darken(float s, struct hwd_colour in) {
    assert(s >= 0.0 && s <= 1.0);
    struct hwd_colour out = {
        (1 - s) * in.r,
        (1 - s) * in.g,
        (1 - s) * in.b,
        in.a,
    };
    return out;
}
