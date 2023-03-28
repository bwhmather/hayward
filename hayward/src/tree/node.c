#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "hayward/tree/node.h"

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <hayward/output.h>
#include <hayward/server.h>
#include <hayward/tree/column.h>
#include <hayward/tree/root.h>
#include <hayward/tree/window.h>
#include <hayward/tree/workspace.h>

#include <config.h>

void
node_init(struct hayward_node *node, enum hayward_node_type type, void *thing) {
    static size_t next_id = 1;
    node->id = next_id++;
    node->type = type;
    node->hayward_root = thing;
    wl_signal_init(&node->events.begin_destroy);
}
