#ifndef _SWAY_WINDOW_H
#define _SWAY_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_container *window_create(struct sway_view *view);

#endif
