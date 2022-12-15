#ifndef _HAYWARD_DESKTOP_IDLE_INHIBIT_V1_H
#define _HAYWARD_DESKTOP_IDLE_INHIBIT_V1_H
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle.h>
#include "hayward/server.h"

enum hayward_idle_inhibit_mode {
	INHIBIT_IDLE_APPLICATION,  // Application set inhibitor (when visible)
	INHIBIT_IDLE_FOCUS,  // User set inhibitor when focused
	INHIBIT_IDLE_FULLSCREEN,  // User set inhibitor when fullscreen + visible
	INHIBIT_IDLE_OPEN,  // User set inhibitor while open
	INHIBIT_IDLE_VISIBLE  // User set inhibitor when visible
};

struct hayward_idle_inhibit_manager_v1 {
	struct wlr_idle_inhibit_manager_v1 *wlr_manager;
	struct wl_listener new_idle_inhibitor_v1;
	struct wl_list inhibitors;

	struct wlr_idle *idle;
};

struct hayward_idle_inhibitor_v1 {
	struct hayward_idle_inhibit_manager_v1 *manager;
	struct wlr_idle_inhibitor_v1 *wlr_inhibitor;
	struct hayward_view *view;
	enum hayward_idle_inhibit_mode mode;

	struct wl_list link;
	struct wl_listener destroy;
};

struct hayward_idle_inhibit_manager_v1 *hayward_idle_inhibit_manager_v1_create(
	struct wl_display *wl_display, struct wlr_idle *idle);

void hayward_idle_inhibit_v1_check_active(
	struct hayward_idle_inhibit_manager_v1 *manager);

void hayward_idle_inhibit_v1_user_inhibitor_destroy(
		struct hayward_idle_inhibitor_v1 *inhibitor);

void hayward_idle_inhibit_v1_user_inhibitor_register(struct hayward_view *view,
		enum hayward_idle_inhibit_mode mode);

struct hayward_idle_inhibitor_v1 *hayward_idle_inhibit_v1_user_inhibitor_for_view(
		struct hayward_view *view);

struct hayward_idle_inhibitor_v1 *hayward_idle_inhibit_v1_application_inhibitor_for_view(
		struct hayward_view *view);

bool hayward_idle_inhibit_v1_is_active(
	struct hayward_idle_inhibitor_v1 *inhibitor);

#endif
