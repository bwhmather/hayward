#ifndef _HAYWARDBAR_TRAY_WATCHER_H
#define _HAYWARDBAR_TRAY_WATCHER_H

#include "hayward-common/list.h"

#include "haywardbar/tray/tray.h"

struct haywardbar_watcher {
	char *interface;
	sd_bus *bus;
	list_t *hosts;
	list_t *items;
	int version;
};

struct haywardbar_watcher *create_watcher(char *protocol, sd_bus *bus);
void destroy_watcher(struct haywardbar_watcher *watcher);

#endif
