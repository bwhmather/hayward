#ifndef _HAYWARDBAR_TRAY_TRAY_H
#define _HAYWARDBAR_TRAY_TRAY_H

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
#include "haywardbar/tray/host.h"
#include "hayward-common/list.h"

struct haywardbar;
struct haywardbar_output;
struct haywardbar_watcher;

struct haywardbar_tray {
	struct haywardbar *bar;

	int fd;
	sd_bus *bus;

	struct haywardbar_host host_xdg;
	struct haywardbar_host host_kde;
	list_t *items; // struct haywardbar_sni *
	struct haywardbar_watcher *watcher_xdg;
	struct haywardbar_watcher *watcher_kde;

	list_t *basedirs; // char *
	list_t *themes; // struct haywardbar_theme *
};

struct haywardbar_tray *create_tray(struct haywardbar *bar);
void destroy_tray(struct haywardbar_tray *tray);
void tray_in(int fd, short mask, void *data);
uint32_t render_tray(cairo_t *cairo, struct haywardbar_output *output, double *x);

#endif
