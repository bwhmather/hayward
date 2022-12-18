#ifndef _HAYWARD_HAYWARDNAG_H
#define _HAYWARD_HAYWARDNAG_H
#include <wayland-server-core.h>

struct haywardnag_instance {
    struct wl_client *client;
    struct wl_listener client_destroy;

    const char *args;
    int fd[2];
    bool detailed;
};

// Spawn haywardnag. If haywardnag->detailed, then haywardnag->fd[1] will left
// open so it can be written to. Call haywardnag_show when done writing. This
// will be automatically called by haywardnag_log if the instance is not spawned
// and haywardnag->detailed is true.
bool
haywardnag_spawn(
    const char *haywardnag_command, struct haywardnag_instance *haywardnag
);

// Write a log message to haywardnag->fd[1]. This will fail when
// haywardnag->detailed is false.
void
haywardnag_log(
    const char *haywardnag_command, struct haywardnag_instance *haywardnag,
    const char *fmt, ...
);

// If haywardnag->detailed, close haywardnag->fd[1] so haywardnag displays
void
haywardnag_show(struct haywardnag_instance *haywardnag);

#endif
