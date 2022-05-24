#ifndef _WMIIVBAR_TRAY_ITEM_H
#define _WMIIVBAR_TRAY_ITEM_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include "wmiivbar/tray/tray.h"
#include "list.h"

struct wmiivbar_output;

struct wmiivbar_pixmap {
	int size;
	unsigned char pixels[];
};

struct wmiivbar_sni_slot {
	struct wl_list link; // wmiivbar_sni::slots
	struct wmiivbar_sni *sni;
	const char *prop;
	const char *type;
	void *dest;
	sd_bus_slot *slot;
};

struct wmiivbar_sni {
	// icon properties
	struct wmiivbar_tray *tray;
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
	list_t *icon_pixmap; // struct wmiivbar_pixmap *
	char *attention_icon_name;
	list_t *attention_icon_pixmap; // struct wmiivbar_pixmap *
	bool item_is_menu;
	char *menu;
	char *icon_theme_path; // non-standard KDE property

	struct wl_list slots; // wmiivbar_sni_slot::link
};

struct wmiivbar_sni *create_sni(char *id, struct wmiivbar_tray *tray);
void destroy_sni(struct wmiivbar_sni *sni);
uint32_t render_sni(cairo_t *cairo, struct wmiivbar_output *output, double *x,
		struct wmiivbar_sni *sni);

#endif
