#include "hwdout-window.h"

#include "hwdout-output-manager.h"

#include <gtk/gtk.h>

struct _HwdoutWindow {
    GtkWindow parent_instance;

    HwdoutOutputManager *output_manager;
};

G_DEFINE_TYPE(HwdoutWindow, hwdout_window, GTK_TYPE_WINDOW)

typedef enum { PROP_OUTPUT_MANAGER = 1, N_PROPERTIES } HwdoutWindowProperty;

static GParamSpec *properties[N_PROPERTIES];

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
        g_clear_object(&self->output_manager);
        self->output_manager = g_value_dup_object(value);
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
        HWDOUT_TYPE_OUTPUT_MANAGER, G_PARAM_READWRITE
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
