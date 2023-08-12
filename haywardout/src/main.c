#include "hwdout-output-manager.h"
#include "hwdout-window.h"

#include <gdk/wayland/gdkwayland.h>
#include <gtk/gtk.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _Hwdout {
    GtkApplication *application;
    HwdoutOutputManager *output_manager;
    HwdoutWindow *window;
};
typedef struct _Hwdout Hwdout;

static void
handle_global(
    void *data, struct wl_registry *wl_registry, uint32_t id, const char *interface,
    uint32_t version
) {
    Hwdout *hwdout = (Hwdout *)data;

    struct zwlr_output_manager_v1 *wlr_output_manager;
    HwdoutOutputManager *output_manager;

    GValue output_manager_value = G_VALUE_INIT;

    g_debug("global: %s", interface);
    if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        wlr_output_manager =
            wl_registry_bind(wl_registry, id, &zwlr_output_manager_v1_interface, 4);

        output_manager = hwdout_output_manager_new(wlr_output_manager);
        g_return_if_fail(HWDOUT_IS_OUTPUT_MANAGER(output_manager));

        g_clear_object(&hwdout->output_manager);
        hwdout->output_manager = output_manager;

        if (hwdout->window != NULL) {
            g_value_init(&output_manager_value, HWDOUT_TYPE_OUTPUT_MANAGER);
            g_value_set_object(&output_manager_value, output_manager);
            g_object_set_property(
                G_OBJECT(hwdout->window), "output-manager", &output_manager_value
            );
        }
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static gboolean daemon = FALSE;

static GOptionEntry entries[] = {
    {"daemon", 'd', 0, G_OPTION_ARG_NONE, &daemon, "Run the haywardout daemon", NULL},
    G_OPTION_ENTRY_NULL};

static int
handle_local_options(GtkApplication *app, GVariantDict *options, gpointer user_data) {
    GApplicationFlags flags;

    if (daemon) {
        flags = g_application_get_flags(G_APPLICATION(app));
        flags ^= G_APPLICATION_IS_LAUNCHER;
        flags &= G_APPLICATION_IS_SERVICE;
        g_application_set_flags(G_APPLICATION(app), flags);
    }

    return -1;
}

static void
startup(GtkApplication *app, gpointer user_data) {
    Hwdout *hwdout = (Hwdout *)user_data;
    GdkDisplay *gdk_display;

    struct wl_display *wl_display;
    struct wl_registry *wl_registry;

    g_warning("Startup!");

    gdk_display = gdk_display_get_default();
    g_assert(GDK_IS_WAYLAND_DISPLAY(gdk_display));

    wl_display = gdk_wayland_display_get_wl_display(gdk_display);
    g_assert_nonnull(wl_display);

    wl_registry = wl_display_get_registry(wl_display);
    g_assert_nonnull(wl_registry);

    wl_registry_add_listener(wl_registry, &registry_listener, hwdout);
}

static void
shutdown(GtkApplication *app, gpointer user_data) {}

static void
activate(GtkApplication *app, gpointer user_data) {
    Hwdout *hwdout = (Hwdout *)user_data;

    g_application_hold(G_APPLICATION(app));

    if (hwdout->window == NULL) {
        hwdout->window = hwdout_window_new();
        gtk_application_add_window(app, GTK_WINDOW(hwdout->window));
    }
    g_return_if_fail(HWDOUT_IS_WINDOW(hwdout->window));

    gtk_window_present(GTK_WINDOW(hwdout->window));
}

int
main(int argc, char *argv[]) {
    GtkApplication *app;
    Hwdout *hwdout;
    int status;

    app = gtk_application_new("com.bwhmather.haywardout", G_APPLICATION_IS_LAUNCHER);

    g_application_set_option_context_summary(
        G_APPLICATION(app), "Service and GUI for managing hayward output configuration."
    );
    g_application_add_main_option_entries(G_APPLICATION(app), entries);

    hwdout = g_malloc0(sizeof(Hwdout));
    hwdout->application = app;

    g_signal_connect(app, "handle-local-options", G_CALLBACK(handle_local_options), hwdout);
    g_signal_connect(app, "activate", G_CALLBACK(activate), hwdout);
    g_signal_connect(app, "startup", G_CALLBACK(startup), hwdout);
    g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), hwdout);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    exit(status);
}
