#include "hwdout-window.h"

#include "hwdout-configuration-head.h"
#include "hwdout-configuration.h"
#include "hwdout-manager.h"

#include <gtk/gtk.h>

struct _HwdoutWindow {
    GtkWindow parent_instance;

    GtkListView *heads_list_view;

    HwdoutManager *manager;
    gulong manager_done_id;

    HwdoutConfiguration *configuration;
};

G_DEFINE_TYPE(HwdoutWindow, hwdout_window, GTK_TYPE_WINDOW)

typedef enum { PROP_MANAGER = 1, PROP_CONFIGURATION, N_PROPERTIES } HwdoutWindowProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
hwdout_window_reset_configuration(HwdoutWindow *self) {
    GListModel *heads_list;
    GtkNoSelection *heads_selection;

    g_return_if_fail(HWDOUT_IS_WINDOW(self));

    g_clear_object(&self->configuration);
    if (self->manager == NULL) {
        return;
    }

    self->configuration = hwdout_configuration_new(self->manager);
    g_return_if_fail(HWDOUT_IS_CONFIGURATION(self->configuration));

    heads_list = hwdout_configuration_get_heads(self->configuration);
    heads_selection = gtk_no_selection_new(heads_list);
    gtk_list_view_set_model(
        self->heads_list_view,
        GTK_SELECTION_MODEL(heads_selection)
    );
    g_clear_object(&heads_selection);

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_CONFIGURATION]);
}

static void
hwdout_window_dispose(GObject *gobject) {
    HwdoutWindow *self = HWDOUT_WINDOW(gobject);

    gtk_widget_dispose_template(GTK_WIDGET(self), HWDOUT_TYPE_WINDOW);

    g_clear_object(&self->manager);

    G_OBJECT_CLASS(hwdout_window_parent_class)->dispose(gobject);
}

static void
hwdout_window_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutWindow *self = HWDOUT_WINDOW(gobject);

    switch ((HwdoutWindowProperty)property_id) {
    case PROP_MANAGER:
        hwdout_window_set_manager(self, g_value_get_object(value));
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
    case PROP_MANAGER:
        g_value_set_object(value, self->manager);
        break;

    case PROP_CONFIGURATION:
        g_value_set_object(value, self->configuration);
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

    hwdout_configuration_head_get_type();

    object_class->dispose = hwdout_window_dispose;
    object_class->set_property = hwdout_window_set_property;
    object_class->get_property = hwdout_window_get_property;

    properties[PROP_MANAGER] = g_param_spec_object(
        "manager", "Output Manager",
        "Output manager that can be used to interact with the compositor", HWDOUT_TYPE_MANAGER,
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );
    properties[PROP_CONFIGURATION] = g_param_spec_object(
        "configuration", "Output configuration",
        "State object tracking staged changes to the output", HWDOUT_TYPE_CONFIGURATION,
        G_PARAM_READABLE
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    gtk_widget_class_set_template_from_resource(
        widget_class, "/com/bwhmather/hwdout/ui/hwdout-window.ui"
    );
    gtk_widget_class_bind_template_child(widget_class, HwdoutWindow, heads_list_view);
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
handle_manager_done(HwdoutManager *manager, uint32_t serial, void *data) {
    HwdoutWindow *self = HWDOUT_WINDOW(data);

    hwdout_window_reset_configuration(self);
}

void
hwdout_window_set_manager(HwdoutWindow *self, HwdoutManager *manager) {
    g_return_if_fail(HWDOUT_IS_WINDOW(self));

    if (manager == self->manager) {
        return;
    }

    if (self->manager != NULL) {
        g_signal_handler_disconnect(self->manager, self->manager_done_id);
        g_clear_object(&self->manager);
    }

    self->manager = manager;
    if (manager == NULL) {
        return;
    }
    g_signal_connect_object(
        self->manager, "done", G_CALLBACK(handle_manager_done), self, G_CONNECT_DEFAULT
    );

    hwdout_window_reset_configuration(self);

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MANAGER]);
}
