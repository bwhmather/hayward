#include <gdk/wayland/gdkwayland.h>
#include <gtk/gtk.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

static void
handle_global(
    void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version
) {
    g_warning("global: %s", interface);
    if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        g_warning("Success!");
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

    wl_registry_add_listener(wl_registry, &registry_listener, NULL);
}

static void
shutdown(GtkApplication *app, gpointer user_data) {
    g_warning("Shutdown!");
}

static void
activate(GtkApplication *app, gpointer user_data) {
    g_warning("Activate");

    g_application_hold(G_APPLICATION(app));
}

int
main(int argc, char *argv[]) {
    GError *error = NULL;
    GOptionContext *context;
    GtkApplication *app;
    int status;

    context = g_option_context_new("- manage hayward outputs");
    g_option_context_add_main_entries(context, entries, "haywardout");
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_print("option_parsing failed %s\n", error->message);
        exit(1);
    }

    app = gtk_application_new("com.bwhmather.haywardout", G_APPLICATION_IS_LAUNCHER);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(handle_local_options), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(startup), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(shutdown), NULL);

    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    exit(status);
}
