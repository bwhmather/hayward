#include "hwdout-application.h"

#include "hwdout-output-manager.h"
#include "hwdout-window.h"

#include <gdk/wayland/gdkwayland.h>
#include <gtk/gtk.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _HwdoutApplication {
    GObject parent_instance;

    GtkApplication *gtk_app;

    HwdoutOutputManager *output_manager;
    HwdoutWindow *window;
};

G_DEFINE_TYPE(HwdoutApplication, hwdout_application, G_TYPE_OBJECT)

typedef enum {
    PROP_OUTPUT_MANAGER = 1,
    PROP_APPLICATION_ID,
    N_PROPERTIES
} HwdoutApplicationProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
handle_global(
    void *data, struct wl_registry *wl_registry, uint32_t id, const char *interface,
    uint32_t version
) {
    HwdoutApplication *app = HWDOUT_APPLICATION(data);

    struct zwlr_output_manager_v1 *wlr_output_manager;
    HwdoutOutputManager *output_manager;

    GValue output_manager_value = G_VALUE_INIT;

    g_debug("global: %s", interface);
    if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
        wlr_output_manager =
            wl_registry_bind(wl_registry, id, &zwlr_output_manager_v1_interface, 4);

        output_manager = hwdout_output_manager_new(wlr_output_manager);
        g_return_if_fail(HWDOUT_IS_OUTPUT_MANAGER(output_manager));

        g_clear_object(&app->output_manager);
        app->output_manager = output_manager;

        if (app->window != NULL) {
            g_value_init(&output_manager_value, HWDOUT_TYPE_OUTPUT_MANAGER);
            g_value_set_object(&output_manager_value, output_manager);
            g_object_set_property(G_OBJECT(app->window), "output-manager", &output_manager_value);
        }
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static GOptionEntry entries[] = {
    {"daemon", 'd', 0, G_OPTION_ARG_NONE, NULL, "Run the haywardout daemon", NULL},
    G_OPTION_ENTRY_NULL};

static int
handle_local_options(GApplication *g_app, GVariantDict *options, gpointer user_data) {
    HwdoutApplication *self = HWDOUT_APPLICATION(user_data);
    GtkApplication *gtk_app = GTK_APPLICATION(g_app);

    gboolean is_daemon = FALSE;
    GApplicationFlags flags;

    g_return_val_if_fail(HWDOUT_IS_APPLICATION(self), 1);
    g_return_val_if_fail(GTK_IS_APPLICATION(gtk_app), 1);
    g_return_val_if_fail(gtk_app == self->gtk_app, 1);

    g_variant_dict_lookup(options, "daemon", "b", &is_daemon);
    if (is_daemon) {
        flags = g_application_get_flags(G_APPLICATION(gtk_app));
        flags ^= G_APPLICATION_IS_LAUNCHER;
        flags |= G_APPLICATION_IS_SERVICE;
        g_application_set_flags(G_APPLICATION(gtk_app), flags);
    }

    g_variant_dict_clear(options);
    g_variant_dict_init(options, NULL);
    g_variant_dict_insert(options, "open-window", "b", !is_daemon);

    return -1;
}

static void
handle_startup(GApplication *g_app, gpointer user_data) {
    HwdoutApplication *self = HWDOUT_APPLICATION(user_data);
    GtkApplication *gtk_app = GTK_APPLICATION(g_app);

    GdkDisplay *gdk_display;

    struct wl_display *wl_display;
    struct wl_registry *wl_registry;

    g_return_if_fail(HWDOUT_IS_APPLICATION(self));
    g_return_if_fail(GTK_IS_APPLICATION(gtk_app));
    g_return_if_fail(gtk_app == self->gtk_app);

    gdk_display = gdk_display_get_default();
    g_assert(GDK_IS_WAYLAND_DISPLAY(gdk_display));

    wl_display = gdk_wayland_display_get_wl_display(gdk_display);
    g_assert_nonnull(wl_display);

    wl_registry = wl_display_get_registry(wl_display);
    g_assert_nonnull(wl_registry);

    wl_registry_add_listener(wl_registry, &registry_listener, self);

    g_application_hold(G_APPLICATION(gtk_app));
}

static void
handle_shutdown(GApplication *g_app, gpointer user_data) {}

static void
handle_window_removed(GtkApplication *gtk_app, GtkWindow *gtk_window, gpointer user_data) {
    HwdoutApplication *self = HWDOUT_APPLICATION(user_data);
    HwdoutWindow *window;

    g_return_if_fail(HWDOUT_IS_APPLICATION(self));

    if (!HWDOUT_IS_WINDOW(gtk_window)) {
        return;
    }
    window = HWDOUT_WINDOW(gtk_window);
    g_return_if_fail(self->window == window);

    g_clear_object(&self->window);
}

static void
handle_command_line(
    GApplication *g_app, GApplicationCommandLine *command_line, gpointer user_data
) {
    HwdoutApplication *self = HWDOUT_APPLICATION(user_data);
    GtkApplication *gtk_app = GTK_APPLICATION(g_app);

    gboolean open_window = false;
    GVariantDict *options;

    g_return_if_fail(HWDOUT_IS_APPLICATION(self));
    g_return_if_fail(GTK_APPLICATION(gtk_app));
    g_return_if_fail(gtk_app == self->gtk_app);

    options = g_application_command_line_get_options_dict(command_line);
    g_variant_dict_lookup(options, "open-window", "b", &open_window);
    if (!open_window) {
        return;
    }

    if (self->window == NULL) {
        self->window = hwdout_window_new();
        g_object_ref_sink(self->window);

        if (self->output_manager) {
            hwdout_window_set_output_manager(self->window, self->output_manager);
        }

        gtk_application_add_window(gtk_app, GTK_WINDOW(g_object_ref(self->window)));
    }
    g_return_if_fail(HWDOUT_IS_WINDOW(self->window));

    gtk_window_present(GTK_WINDOW(self->window));
}

static void
hwdout_application_dispose(GObject *gobject) {
    HwdoutApplication *self = HWDOUT_APPLICATION(gobject);

    g_clear_object(&self->window);
    g_clear_object(&self->output_manager);

    G_OBJECT_CLASS(hwdout_application_parent_class)->dispose(gobject);
}

static void
hwdout_application_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutApplication *self = HWDOUT_APPLICATION(gobject);

    switch ((HwdoutApplicationProperty)property_id) {
    case PROP_APPLICATION_ID:
        g_object_set_property(G_OBJECT(self->gtk_app), "application-id", value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_application_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutApplication *self = HWDOUT_APPLICATION(gobject);

    switch ((HwdoutApplicationProperty)property_id) {
    case PROP_OUTPUT_MANAGER:
        g_value_set_object(value, self->output_manager);
        break;

    case PROP_APPLICATION_ID:
        g_object_get_property(G_OBJECT(self->gtk_app), "application-id", value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_application_class_init(HwdoutApplicationClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = hwdout_application_dispose;
    object_class->set_property = hwdout_application_set_property;
    object_class->get_property = hwdout_application_get_property;

    properties[PROP_OUTPUT_MANAGER] = g_param_spec_object(
        "output-manager", "Output Manager",
        "Output manager that can be used to interact with the compositor",
        HWDOUT_TYPE_OUTPUT_MANAGER, G_PARAM_READABLE
    );
    properties[PROP_APPLICATION_ID] = g_param_spec_string(
        "application-id", "Application ID", "The unique identifier for the application", "",
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

static void
hwdout_application_init(HwdoutApplication *self) {
    self->gtk_app = gtk_application_new(
        "com.bwhmather.haywardout", G_APPLICATION_IS_LAUNCHER | G_APPLICATION_HANDLES_COMMAND_LINE
    );
    g_return_if_fail(GTK_IS_APPLICATION(self->gtk_app));

    g_application_set_option_context_summary(
        G_APPLICATION(self->gtk_app), "Service and GUI for managing hayward output configuration."
    );
    g_application_add_main_option_entries(G_APPLICATION(self->gtk_app), entries);

    g_signal_connect_object(
        self->gtk_app, "handle-local-options", G_CALLBACK(handle_local_options), self,
        G_CONNECT_DEFAULT
    );
    g_signal_connect_object(
        self->gtk_app, "command-line", G_CALLBACK(handle_command_line), self, G_CONNECT_DEFAULT
    );
    // TODO GtkWindow does not trigger the "destroy" signal when destroyed.  It
    // only notifies the application it is registered to.  This is arguably a
    // but and should bt fixed upstream.
    g_signal_connect_object(
        self->gtk_app, "window-removed", G_CALLBACK(handle_window_removed), self, G_CONNECT_DEFAULT
    );
    g_signal_connect_object(
        self->gtk_app, "startup", G_CALLBACK(handle_startup), self, G_CONNECT_DEFAULT
    );
    g_signal_connect_object(
        self->gtk_app, "shutdown", G_CALLBACK(handle_shutdown), self, G_CONNECT_DEFAULT
    );
}

HwdoutApplication *
hwdout_application_new(const gchar *application_id) {
    return g_object_new(HWDOUT_TYPE_APPLICATION, "application-id", application_id, NULL);
}

int
hwdout_application_run(HwdoutApplication *self, int argc, char *argv[]) {
    g_return_val_if_fail(HWDOUT_IS_APPLICATION(self), 1);

    return g_application_run(G_APPLICATION(self->gtk_app), argc, argv);
}
