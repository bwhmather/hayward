#ifndef _SWAY_COLUMN_H
#define _SWAY_COLUMN_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_container *column_create(void);

void column_consider_destroy(struct sway_container *con);

/**
 * Search a container's descendants a container based on test criteria. Returns
 * the first container that passes the test.
 */
struct sway_container *column_find_child(struct sway_container *container,
		bool (*test)(struct sway_container *view, void *data), void *data);

void column_add_child(struct sway_container *parent,
		struct sway_container *child);

void column_insert_child(struct sway_container *parent,
		struct sway_container *child, int i);

/**
 * Side should be 0 to add before, or 1 to add after.
 */
void column_add_sibling(struct sway_container *parent,
		struct sway_container *child, bool after);


#endif
