#ifndef _WMIIVBAR_TRAY_TRAY_H
#define _WMIIVBAR_TRAY_TRAY_H

#include "config.h"
#if HAVE_LIBSYSTEMD
#include <systemd/sd-bus.h>
#elif HAVE_LIBELOGIND
#include <elogind/sd-bus.h>
#elif HAVE_BASU
#include <basu/sd-bus.h>
#endif
#include <cairo.h>
#include <stdint.h>
#include "wmiivbar/tray/host.h"
#include "list.h"

struct wmiivbar;
struct wmiivbar_output;
struct wmiivbar_watcher;

struct wmiivbar_tray {
	struct wmiivbar *bar;

	int fd;
	sd_bus *bus;

	struct wmiivbar_host host_xdg;
	struct wmiivbar_host host_kde;
	list_t *items; // struct wmiivbar_sni *
	struct wmiivbar_watcher *watcher_xdg;
	struct wmiivbar_watcher *watcher_kde;

	list_t *basedirs; // char *
	list_t *themes; // struct wmiivbar_theme *
};

struct wmiivbar_tray *create_tray(struct wmiivbar *bar);
void destroy_tray(struct wmiivbar_tray *tray);
void tray_in(int fd, short mask, void *data);
uint32_t render_tray(cairo_t *cairo, struct wmiivbar_output *output, double *x);

#endif
