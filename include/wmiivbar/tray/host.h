#ifndef _SWAYBAR_TRAY_HOST_H
#define _SWAYBAR_TRAY_HOST_H

#include <stdbool.h>

struct wmiivbar_tray;

struct wmiivbar_host {
	struct wmiivbar_tray *tray;
	char *service;
	char *watcher_interface;
};

bool init_host(struct wmiivbar_host *host, char *protocol, struct wmiivbar_tray *tray);
void finish_host(struct wmiivbar_host *host);

#endif
