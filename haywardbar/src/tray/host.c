#define _POSIX_C_SOURCE 200809L
#include "haywardbar/tray/host.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hayward-common/list.h"
#include "hayward-common/log.h"

#include "haywardbar/bar.h"
#include "haywardbar/tray/item.h"
#include "haywardbar/tray/tray.h"

static const char *watcher_path = "/StatusNotifierWatcher";

static int cmp_sni_id(const void *item, const void *cmp_to) {
	const struct haywardbar_sni *sni = item;
	return strcmp(sni->watcher_id, cmp_to);
}

static void add_sni(struct haywardbar_tray *tray, char *id) {
	int idx = list_seq_find(tray->items, cmp_sni_id, id);
	if (idx == -1) {
		hayward_log(HAYWARD_INFO, "Registering Status Notifier Item '%s'", id);
		struct haywardbar_sni *sni = create_sni(id, tray);
		if (sni) {
			list_add(tray->items, sni);
		}
	}
}

static int
handle_sni_registered(sd_bus_message *msg, void *data, sd_bus_error *error) {
	char *id;
	int ret = sd_bus_message_read(msg, "s", &id);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to parse register SNI message: %s",
			strerror(-ret)
		);
	}

	struct haywardbar_tray *tray = data;
	add_sni(tray, id);

	return ret;
}

static int
handle_sni_unregistered(sd_bus_message *msg, void *data, sd_bus_error *error) {
	char *id;
	int ret = sd_bus_message_read(msg, "s", &id);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to parse unregister SNI message: %s",
			strerror(-ret)
		);
	}

	struct haywardbar_tray *tray = data;
	int idx = list_seq_find(tray->items, cmp_sni_id, id);
	if (idx != -1) {
		hayward_log(
			HAYWARD_INFO, "Unregistering Status Notifier Item '%s'", id
		);
		destroy_sni(tray->items->items[idx]);
		list_del(tray->items, idx);
		set_bar_dirty(tray->bar);
	}
	return ret;
}

static int get_registered_snis_callback(
	sd_bus_message *msg, void *data, sd_bus_error *error
) {
	if (sd_bus_message_is_method_error(msg, NULL)) {
		const sd_bus_error *err = sd_bus_message_get_error(msg);
		hayward_log(
			HAYWARD_ERROR, "Failed to get registered SNIs: %s", err->message
		);
		return -sd_bus_error_get_errno(err);
	}

	int ret = sd_bus_message_enter_container(msg, 'v', NULL);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to read registered SNIs: %s", strerror(-ret)
		);
		return ret;
	}

	char **ids;
	ret = sd_bus_message_read_strv(msg, &ids);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to read registered SNIs: %s", strerror(-ret)
		);
		return ret;
	}

	if (ids) {
		struct haywardbar_tray *tray = data;
		for (char **id = ids; *id; ++id) {
			add_sni(tray, *id);
			free(*id);
		}
	}

	free(ids);
	return ret;
}

static bool register_to_watcher(struct haywardbar_host *host) {
	// this is called asynchronously in case the watcher is owned by this
	// process
	int ret = sd_bus_call_method_async(
		host->tray->bus, NULL, host->watcher_interface, watcher_path,
		host->watcher_interface, "RegisterStatusNotifierHost", NULL, NULL, "s",
		host->service
	);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to send register call: %s", strerror(-ret)
		);
		return false;
	}

	ret = sd_bus_call_method_async(
		host->tray->bus, NULL, host->watcher_interface, watcher_path,
		"org.freedesktop.DBus.Properties", "Get", get_registered_snis_callback,
		host->tray, "ss", host->watcher_interface,
		"RegisteredStatusNotifierItems"
	);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to get registered SNIs: %s", strerror(-ret)
		);
	}

	return ret >= 0;
}

static int
handle_new_watcher(sd_bus_message *msg, void *data, sd_bus_error *error) {
	char *service, *old_owner, *new_owner;
	int ret = sd_bus_message_read(msg, "sss", &service, &old_owner, &new_owner);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to parse owner change message: %s",
			strerror(-ret)
		);
		return ret;
	}

	if (!*old_owner) {
		struct haywardbar_host *host = data;
		if (strcmp(service, host->watcher_interface) == 0) {
			register_to_watcher(host);
		}
	}

	return 0;
}

bool init_host(
	struct haywardbar_host *host, char *protocol, struct haywardbar_tray *tray
) {
	size_t len =
		snprintf(NULL, 0, "org.%s.StatusNotifierWatcher", protocol) + 1;
	host->watcher_interface = malloc(len);
	if (!host->watcher_interface) {
		return false;
	}
	snprintf(
		host->watcher_interface, len, "org.%s.StatusNotifierWatcher", protocol
	);

	sd_bus_slot *reg_slot = NULL, *unreg_slot = NULL, *watcher_slot = NULL;
	int ret = sd_bus_match_signal(
		tray->bus, &reg_slot, host->watcher_interface, watcher_path,
		host->watcher_interface, "StatusNotifierItemRegistered",
		handle_sni_registered, tray
	);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to subscribe to registering events: %s",
			strerror(-ret)
		);
		goto error;
	}
	ret = sd_bus_match_signal(
		tray->bus, &unreg_slot, host->watcher_interface, watcher_path,
		host->watcher_interface, "StatusNotifierItemUnregistered",
		handle_sni_unregistered, tray
	);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to subscribe to unregistering events: %s",
			strerror(-ret)
		);
		goto error;
	}

	ret = sd_bus_match_signal(
		tray->bus, &watcher_slot, "org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "NameOwnerChanged",
		handle_new_watcher, host
	);
	if (ret < 0) {
		hayward_log(
			HAYWARD_ERROR, "Failed to subscribe to unregistering events: %s",
			strerror(-ret)
		);
		goto error;
	}

	pid_t pid = getpid();
	size_t service_len =
		snprintf(NULL, 0, "org.%s.StatusNotifierHost-%d", protocol, pid) + 1;
	host->service = malloc(service_len);
	if (!host->service) {
		goto error;
	}
	snprintf(
		host->service, service_len, "org.%s.StatusNotifierHost-%d", protocol,
		pid
	);
	ret = sd_bus_request_name(tray->bus, host->service, 0);
	if (ret < 0) {
		hayward_log(
			HAYWARD_DEBUG, "Failed to acquire service name: %s", strerror(-ret)
		);
		goto error;
	}

	host->tray = tray;
	if (!register_to_watcher(host)) {
		goto error;
	}

	sd_bus_slot_set_floating(reg_slot, 0);
	sd_bus_slot_set_floating(unreg_slot, 0);
	sd_bus_slot_set_floating(watcher_slot, 0);

	hayward_log(HAYWARD_DEBUG, "Registered %s", host->service);
	return true;
error:
	sd_bus_slot_unref(reg_slot);
	sd_bus_slot_unref(unreg_slot);
	sd_bus_slot_unref(watcher_slot);
	finish_host(host);
	return false;
}

void finish_host(struct haywardbar_host *host) {
	sd_bus_release_name(host->tray->bus, host->service);
	free(host->service);
	free(host->watcher_interface);
}
