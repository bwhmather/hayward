#ifndef _SWAY_WINDOW_H
#define _SWAY_WINDOW_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "sway/tree/node.h"

struct sway_container *window_create(struct sway_view *view);

/**
 * Find any container that has the given mark and return it.
 */
struct sway_container *window_find_mark(char *mark);

/**
 * Find any container that has the given mark and remove the mark from the
 * container. Returns true if it matched a container.
 */
bool window_find_and_unmark(char *mark);

/**
 * Remove all marks from the container.
 */
void window_clear_marks(struct sway_container *container);

bool window_has_mark(struct sway_container *container, char *mark);

void window_add_mark(struct sway_container *container, char *mark);

void window_update_marks_textures(struct sway_container *container);

void window_set_floating(struct sway_container *container, bool enable);

#endif
