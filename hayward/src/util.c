#define _POSIX_C_SOURCE 200809L
#include "hayward/util.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <hayward/log.h>

bool
parse_color(const char *color, uint32_t *result) {
    if (color[0] == '#') {
        ++color;
    }
    int len = strlen(color);
    if ((len != 6 && len != 8) || !isxdigit(color[0]) || !isxdigit(color[1])) {
        return false;
    }
    char *ptr;
    uint32_t parsed = strtoul(color, &ptr, 16);
    if (*ptr != '\0') {
        return false;
    }
    *result = len == 6 ? ((parsed << 8) | 0xFF) : parsed;
    return true;
}

bool
parse_boolean(const char *boolean, bool current) {
    if (strcasecmp(boolean, "1") == 0 || strcasecmp(boolean, "yes") == 0 ||
        strcasecmp(boolean, "on") == 0 || strcasecmp(boolean, "true") == 0 ||
        strcasecmp(boolean, "enable") == 0 || strcasecmp(boolean, "enabled") == 0 ||
        strcasecmp(boolean, "active") == 0) {
        return true;
    } else if (strcasecmp(boolean, "toggle") == 0) {
        return !current;
    }
    // All other values are false to match i3
    return false;
}

float
parse_float(const char *value) {
    errno = 0;
    char *end;
    float flt = strtof(value, &end);
    if (*end || errno) {
        hwd_log(HWD_DEBUG, "Invalid float value '%s', defaulting to NAN", value);
        return NAN;
    }
    return flt;
}

static enum movement_unit
parse_movement_unit(const char *unit) {
    if (strcasecmp(unit, "px") == 0) {
        return MOVEMENT_UNIT_PX;
    }
    if (strcasecmp(unit, "ppt") == 0) {
        return MOVEMENT_UNIT_PPT;
    }
    if (strcasecmp(unit, "default") == 0) {
        return MOVEMENT_UNIT_DEFAULT;
    }
    return MOVEMENT_UNIT_INVALID;
}

int
parse_movement_amount(int argc, char **argv, struct movement_amount *amount) {
    hwd_assert(argc > 0, "Expected args in parse_movement_amount");

    char *err;
    amount->amount = (int)strtol(argv[0], &err, 10);
    if (*err) {
        // e.g. 10px
        amount->unit = parse_movement_unit(err);
        return 1;
    }
    if (argc == 1) {
        amount->unit = MOVEMENT_UNIT_DEFAULT;
        return 1;
    }
    // Try the second argument
    amount->unit = parse_movement_unit(argv[1]);
    if (amount->unit == MOVEMENT_UNIT_INVALID) {
        amount->unit = MOVEMENT_UNIT_DEFAULT;
        return 1;
    }
    return 2;
}

bool
hwd_set_cloexec(int fd, bool cloexec) {
    int flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        hwd_log_errno(HWD_ERROR, "fcntl failed");
        return false;
    }
    if (cloexec) {
        flags = flags | FD_CLOEXEC;
    } else {
        flags = flags & ~FD_CLOEXEC;
    }
    if (fcntl(fd, F_SETFD, flags) == -1) {
        hwd_log_errno(HWD_ERROR, "fcntl failed");
        return false;
    }
    return true;
}
