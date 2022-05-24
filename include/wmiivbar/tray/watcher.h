#ifndef _WMIIVBAR_TRAY_WATCHER_H
#define _WMIIVBAR_TRAY_WATCHER_H

#include "wmiivbar/tray/tray.h"
#include "list.h"

struct wmiivbar_watcher {
	char *interface;
	sd_bus *bus;
	list_t *hosts;
	list_t *items;
	int version;
};

struct wmiivbar_watcher *create_watcher(char *protocol, sd_bus *bus);
void destroy_watcher(struct wmiivbar_watcher *watcher);

#endif
