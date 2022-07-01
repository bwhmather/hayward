#include <cairo.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "haywardbar/config.h"
#include "haywardbar/bar.h"
#include "haywardbar/tray/icon.h"
#include "haywardbar/tray/host.h"
#include "haywardbar/tray/item.h"
#include "haywardbar/tray/tray.h"
#include "haywardbar/tray/watcher.h"
#include "list.h"
#include "log.h"

static int handle_lost_watcher(sd_bus_message *msg,
		void *data, sd_bus_error *error) {
	char *service, *old_owner, *new_owner;
	int ret = sd_bus_message_read(msg, "sss", &service, &old_owner, &new_owner);
	if (ret < 0) {
		hayward_log(HAYWARD_ERROR, "Failed to parse owner change message: %s", strerror(-ret));
		return ret;
	}

	if (!*new_owner) {
		struct haywardbar_tray *tray = data;
		if (strcmp(service, "org.freedesktop.StatusNotifierWatcher") == 0) {
			tray->watcher_xdg = create_watcher("freedesktop", tray->bus);
		} else if (strcmp(service, "org.kde.StatusNotifierWatcher") == 0) {
			tray->watcher_kde = create_watcher("kde", tray->bus);
		}
	}

	return 0;
}

struct haywardbar_tray *create_tray(struct haywardbar *bar) {
	hayward_log(HAYWARD_DEBUG, "Initializing tray");

	sd_bus *bus;
	int ret = sd_bus_open_user(&bus);
	if (ret < 0) {
		hayward_log(HAYWARD_ERROR, "Failed to connect to user bus: %s", strerror(-ret));
		return NULL;
	}

	struct haywardbar_tray *tray = calloc(1, sizeof(struct haywardbar_tray));
	if (!tray) {
		return NULL;
	}
	tray->bar = bar;
	tray->bus = bus;
	tray->fd = sd_bus_get_fd(tray->bus);

	tray->watcher_xdg = create_watcher("freedesktop", tray->bus);
	tray->watcher_kde = create_watcher("kde", tray->bus);

	ret = sd_bus_match_signal(bus, NULL, "org.freedesktop.DBus",
			"/org/freedesktop/DBus", "org.freedesktop.DBus",
			"NameOwnerChanged", handle_lost_watcher, tray);
	if (ret < 0) {
		hayward_log(HAYWARD_ERROR, "Failed to subscribe to unregistering events: %s",
				strerror(-ret));
	}

	tray->items = create_list();

	init_host(&tray->host_xdg, "freedesktop", tray);
	init_host(&tray->host_kde, "kde", tray);

	init_themes(&tray->themes, &tray->basedirs);

	return tray;
}

void destroy_tray(struct haywardbar_tray *tray) {
	if (!tray) {
		return;
	}
	finish_host(&tray->host_xdg);
	finish_host(&tray->host_kde);
	for (int i = 0; i < tray->items->length; ++i) {
		destroy_sni(tray->items->items[i]);
	}
	list_free(tray->items);
	destroy_watcher(tray->watcher_xdg);
	destroy_watcher(tray->watcher_kde);
	sd_bus_flush_close_unref(tray->bus);
	finish_themes(tray->themes, tray->basedirs);
	free(tray);
}

void tray_in(int fd, short mask, void *data) {
	sd_bus *bus = data;
	int ret;
	while ((ret = sd_bus_process(bus, NULL)) > 0) {
		// This space intentionally left blank
	}
	if (ret < 0) {
		hayward_log(HAYWARD_ERROR, "Failed to process bus: %s", strerror(-ret));
	}
}

static int cmp_output(const void *item, const void *cmp_to) {
	const struct haywardbar_output *output = cmp_to;
	if (output->identifier && strcmp(item, output->identifier) == 0) {
		return 0;
	}
	return strcmp(item, output->name);
}

uint32_t render_tray(cairo_t *cairo, struct haywardbar_output *output, double *x) {
	struct haywardbar_config *config = output->bar->config;
	if (config->tray_outputs) {
		if (list_seq_find(config->tray_outputs, cmp_output, output) == -1) {
			return 0;
		}
	} // else display on all

	if ((int)(output->height * output->scale) <= 2 * config->tray_padding) {
		return (2 * config->tray_padding + 1) / output->scale;
	}

	uint32_t max_height = 0;
	struct haywardbar_tray *tray = output->bar->tray;
	for (int i = 0; i < tray->items->length; ++i) {
		uint32_t h = render_sni(cairo, output, x, tray->items->items[i]);
		if (h > max_height) {
			max_height = h;
		}
	}

	return max_height;
}
