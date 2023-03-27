#ifndef _HAYWARD_NODE_H
#define _HAYWARD_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>

#include <hayward-common/list.h>

#define MIN_SANE_W 100
#define MIN_SANE_H 60

struct hayward_root;
struct hayward_output;
struct hayward_workspace;
struct hayward_window;
struct wlr_box;

enum hayward_node_type {
    N_ROOT,
    N_OUTPUT,
    N_WORKSPACE,
    N_COLUMN,
    N_WINDOW,
};

struct hayward_node {
    enum hayward_node_type type;
    union {
        struct hayward_root *hayward_root;
        struct hayward_output *hayward_output;
        struct hayward_workspace *hayward_workspace;
        struct hayward_column *hayward_column;
        struct hayward_window *hayward_window;
    };

    /**
     * A unique ID to identify this node.
     * Primarily used in the get_tree JSON output.
     */
    size_t id;

    struct {
        struct wl_signal destroy;
    } events;
};

void
node_init(struct hayward_node *node, enum hayward_node_type type, void *thing);

#endif
