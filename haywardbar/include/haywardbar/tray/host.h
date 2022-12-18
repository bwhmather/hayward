#ifndef _HAYWARDBAR_TRAY_HOST_H
#define _HAYWARDBAR_TRAY_HOST_H

#include <stdbool.h>

struct haywardbar_tray;

struct haywardbar_host {
    struct haywardbar_tray *tray;
    char *service;
    char *watcher_interface;
};

bool
init_host(
    struct haywardbar_host *host, char *protocol, struct haywardbar_tray *tray
);
void
finish_host(struct haywardbar_host *host);

#endif
