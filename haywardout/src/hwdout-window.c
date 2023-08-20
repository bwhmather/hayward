#include "hwdout-window.h"

#include "hwdout-output-configuration.h"
#include "hwdout-output-manager.h"

#include <gtk/gtk.h>

struct _HwdoutWindow {
    GtkWindow parent_instance;

    HwdoutOutputManager *output_manager;
    gulong output_manager_done_id;

    HwdoutOutputConfiguration *output_configuration;
};

G_DEFINE_TYPE(HwdoutWindow, hwdout_window, GTK_TYPE_WINDOW)

typedef enum {
    PROP_OUTPUT_MANAGER = 1,
    PROP_OUTPUT_CONFIGURATION,
    N_PROPERTIES
} HwdoutWindowProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
hwdout_window_reset_output_configuration(HwdoutWindow *self) {
    g_return_if_fail(HWDOUT_IS_WINDOW(self));

    g_clear_object(&self->output_configuration);
    if (self->output_manager == NULL) {
        return;
    }

    self->output_configuration = hwdout_output_configuration_new(self->output_manager);

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_OUTPUT_CONFIGURATION]);
}

static void
hwdout_window_dispose(GObject *gobject) {
    HwdoutWindow *self = HWDOUT_WINDOW(gobject);

    gtk_widget_dispose_template(GTK_WIDGET(self), HWDOUT_TYPE_WINDOW);

    g_clear_object(&self->output_manager);

    G_OBJECT_CLASS(hwdout_window_parent_class)->dispose(gobject);
}

static void
hwdout_window_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutWindow *self = HWDOUT_WINDOW(gobject);

    switch ((HwdoutWindowProperty)property_id) {
    case PROP_OUTPUT_MANAGER:
        hwdout_window_set_output_manager(self, g_value_get_object(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_window_get_property(GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec) {
    HwdoutWindow *self = HWDOUT_WINDOW(gobject);

    switch ((HwdoutWindowProperty)property_id) {
    case PROP_OUTPUT_MANAGER:
        g_value_set_object(value, self->output_manager);
        break;

    case PROP_OUTPUT_CONFIGURATION:
        g_value_set_object(value, self->output_configuration);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_window_class_init(HwdoutWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = hwdout_window_dispose;
    object_class->set_property = hwdout_window_set_property;
    object_class->get_property = hwdout_window_get_property;

    properties[PROP_OUTPUT_MANAGER] = g_param_spec_object(
        "output-manager", "Output Manager",
        "Output manager that can be used to interact with the compositor",
        HWDOUT_TYPE_OUTPUT_MANAGER, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );
    properties[PROP_OUTPUT_CONFIGURATION] = g_param_spec_object(
        "output-configuration", "Output configuration",
        "State object tracking staged changes to the output", HWDOUT_TYPE_OUTPUT_CONFIGURATION,
        G_PARAM_READABLE
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(
        widget_class, "/com/bwhmather/hwdout/ui/hwdout-window.ui"
    );
}

static void
hwdout_window_init(HwdoutWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}

HwdoutWindow *
hwdout_window_new() {
    return g_object_new(HWDOUT_TYPE_WINDOW, NULL);
}

static void
handle_manager_done(HwdoutOutputManager *manager, uint32_t serial, void *data) {
    HwdoutWindow *self = HWDOUT_WINDOW(data);

    hwdout_window_reset_output_configuration(self);
}

void
hwdout_window_set_output_manager(HwdoutWindow *self, HwdoutOutputManager *output_manager) {
    g_return_if_fail(HWDOUT_IS_WINDOW(self));

    if (output_manager == self->output_manager) {
        return;
    }

    if (self->output_manager != NULL) {
        g_signal_handler_disconnect(self->output_manager, self->output_manager_done_id);
        g_clear_object(&self->output_manager);
    }

    self->output_manager = output_manager;
    if (output_manager == NULL) {
        return;
    }
    g_signal_connect_object(
        self->output_manager, "done", G_CALLBACK(handle_manager_done), self, G_CONNECT_DEFAULT
    );

    hwdout_window_reset_output_configuration(self);

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_OUTPUT_MANAGER]);
}
