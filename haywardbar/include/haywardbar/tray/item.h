#ifndef _HAYWARDBAR_TRAY_ITEM_H
#define _HAYWARDBAR_TRAY_ITEM_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "haywardbar/tray/tray.h"
#include "list.h"

struct haywardbar_output;

struct haywardbar_pixmap {
	int size;
	unsigned char pixels[];
};

struct haywardbar_sni_slot {
	struct wl_list link; // haywardbar_sni::slots
	struct haywardbar_sni *sni;
	const char *prop;
	const char *type;
	void *dest;
	sd_bus_slot *slot;
};

struct haywardbar_sni {
	// icon properties
	struct haywardbar_tray *tray;
	cairo_surface_t *icon;
	int min_size;
	int max_size;
	int target_size;

	// dbus properties
	char *watcher_id;
	char *service;
	char *path;
	char *interface;

	char *status;
	char *icon_name;
	list_t *icon_pixmap; // struct haywardbar_pixmap *
	char *attention_icon_name;
	list_t *attention_icon_pixmap; // struct haywardbar_pixmap *
	bool item_is_menu;
	char *menu;
	char *icon_theme_path; // non-standard KDE property

	struct wl_list slots; // haywardbar_sni_slot::link
};

struct haywardbar_sni *create_sni(char *id, struct haywardbar_tray *tray);
void destroy_sni(struct haywardbar_sni *sni);
uint32_t render_sni(cairo_t *cairo, struct haywardbar_output *output, double *x,
		struct haywardbar_sni *sni);

#endif
