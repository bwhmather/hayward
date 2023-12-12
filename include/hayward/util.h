#ifndef HWD_UTIL_H
#define HWD_UTIL_H

#include <stdbool.h>

enum movement_unit {
    MOVEMENT_UNIT_PX,
    MOVEMENT_UNIT_PPT,
    MOVEMENT_UNIT_DEFAULT,
    MOVEMENT_UNIT_INVALID,
};

struct movement_amount {
    int amount;
    enum movement_unit unit;
};

/**
 * Given a string that represents a boolean, return the boolean value. This
 * function also takes in the current boolean value to support toggling. If
 * toggling is not desired, pass in true for current so that toggling values
 * get parsed as not true.
 */
bool
parse_boolean(const char *boolean, bool current);

/**
 * Given a string that represents a floating point value, return a float.
 * Returns NAN on error.
 */
float
parse_float(const char *value);

/*
 * Parse arguments such as "10", "10px" or "10 px".
 * Returns the number of arguments consumed.
 */
int
parse_movement_amount(int argc, char **argv, struct movement_amount *amount);

bool
hwd_set_cloexec(int fd, bool cloexec);

#endif
