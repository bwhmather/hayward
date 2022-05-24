#ifndef _WMIIV_WMIIVNAG_H
#define _WMIIV_WMIIVNAG_H
#include <wayland-server-core.h>

struct wmiivnag_instance {
	struct wl_client *client;
	struct wl_listener client_destroy;

	const char *args;
	int fd[2];
	bool detailed;
};

// Spawn wmiivnag. If wmiivnag->detailed, then wmiivnag->fd[1] will left open
// so it can be written to. Call wmiivnag_show when done writing. This will
// be automatically called by wmiivnag_log if the instance is not spawned and
// wmiivnag->detailed is true.
bool wmiivnag_spawn(const char *wmiivnag_command,
		struct wmiivnag_instance *wmiivnag);

// Write a log message to wmiivnag->fd[1]. This will fail when wmiivnag->detailed
// is false.
void wmiivnag_log(const char *wmiivnag_command, struct wmiivnag_instance *wmiivnag,
		const char *fmt, ...);

// If wmiivnag->detailed, close wmiivnag->fd[1] so wmiivnag displays
void wmiivnag_show(struct wmiivnag_instance *wmiivnag);

#endif
