#ifndef _SWAY_COLUMN_H
#define _SWAY_COLUMN_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_container *column_create(void);

void column_consider_destroy(struct sway_container *con);

#endif
