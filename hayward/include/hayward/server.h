#ifndef HWD_SERVER_H
#define HWD_SERVER_H

#include <config.h>

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm_lease_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_text_input_v3.h>

#include <hayward/desktop/layer_shell.h>
#include <hayward/desktop/server_decoration.h>
#include <hayward/desktop/xdg_activation_v1.h>
#include <hayward/desktop/xdg_decoration.h>
#include <hayward/desktop/xdg_shell.h>
#include <hayward/desktop/xwayland.h>

struct hwd_server {
    struct wl_display *wl_display;
    struct wl_event_loop *wl_event_loop;
    const char *socket;

    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_compositor *compositor;

    struct wlr_linux_dmabuf_v1 *linux_dmabuf_v1;

    struct wlr_data_device_manager *data_device_manager;

    struct hwd_input_manager *input;

    struct wl_listener new_output;
    struct wl_listener output_layout_change;

    struct wlr_idle_notifier_v1 *idle_notifier_v1;
    struct hwd_idle_inhibit_manager_v1 *idle_inhibit_manager_v1;

    struct hwd_layer_shell *layer_shell;

    struct hwd_xdg_shell *xdg_shell;

    struct wlr_tablet_manager_v2 *tablet_v2;

#if HAVE_XWAYLAND
    struct hwd_xwayland *xwayland;
#endif

    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

    struct hwd_server_decoration_manager *server_decoration_manager;
    struct hwd_xdg_decoration_manager *xdg_decoration_manager;

    struct wlr_drm_lease_v1_manager *drm_lease_manager;
    struct wl_listener drm_lease_request;

    struct wlr_presentation *presentation;

    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wl_listener pointer_constraint;

    struct {
        bool locked;
        struct wlr_session_lock_manager_v1 *manager;

        struct wlr_session_lock_v1 *lock;
        struct wl_listener lock_new_surface;
        struct wl_listener lock_unlock;
        struct wl_listener lock_destroy;

        struct wl_listener new_lock;
        struct wl_listener manager_destroy;
    } session_lock;

    struct wlr_output_power_manager_v1 *output_power_manager_v1;
    struct wl_listener output_power_manager_set_mode;
    struct wlr_input_method_manager_v2 *input_method;
    struct wlr_text_input_manager_v3 *text_input;
    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

    struct hwd_xdg_activation_v1 *xdg_activation_v1;

    // The timeout for transactions, after which a transaction is applied
    // regardless of readiness.
    size_t txn_timeout_ms;
};

extern struct hwd_server server;

struct hwd_debug {
    bool noatomic;    // Ignore atomic layout updates
    bool txn_timings; // Log verbose messages about transactions
    bool txn_wait;    // Always wait for the timeout before applying
};

extern struct hwd_debug debug;

/* Prepares an unprivileged server_init by performing all privileged operations
 * in advance */
bool
server_privileged_prepare(struct hwd_server *server);
bool
server_init(struct hwd_server *server);
void
server_fini(struct hwd_server *server);
bool
server_start(struct hwd_server *server);
void
server_run(struct hwd_server *server);

void
restore_nofile_limit(void);

void
handle_new_output(struct wl_listener *listener, void *data);

void
hwd_session_lock_init(void);
void
handle_pointer_constraint(struct wl_listener *listener, void *data);

#endif
