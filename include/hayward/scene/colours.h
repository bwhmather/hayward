#ifndef HWD_SCENE_COLOURS_H
#define HWD_SCENE_COLOURS_H

struct hwd_colour {
    float r;
    float g;
    float b;
    float a;
};

struct hwd_colour
hwd_lighten(float s, struct hwd_colour in);

struct hwd_colour
hwd_darken(float s, struct hwd_colour in);

#endif
