#ifndef HWD_HAYWARDNAG_H
#define HWD_HAYWARDNAG_H

#include <stdbool.h>

#include <wayland-server-core.h>

struct haywardnag_instance {
    struct wl_client *client;
    struct wl_listener client_destroy;

    const char *args;
    int fd[2];
    bool detailed;
};

// Write a log message to haywardnag->fd[1]. This will fail when
// haywardnag->detailed is false.
void
haywardnag_log(
    const char *haywardnag_command, struct haywardnag_instance *haywardnag, const char *fmt, ...
);

// If haywardnag->detailed, close haywardnag->fd[1] so haywardnag displays
void
haywardnag_show(struct haywardnag_instance *haywardnag);

#endif
