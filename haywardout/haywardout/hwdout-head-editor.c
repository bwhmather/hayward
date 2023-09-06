#include "hwdout-head-editor.h"

#include <gtk/gtk.h>

#include "hwdout-configuration-head.h"

struct _HwdoutHeadEditor {
    GtkWidget parent_instance;

    HwdoutConfigurationHead *head;
};

G_DEFINE_TYPE(HwdoutHeadEditor, hwdout_head_editor, GTK_TYPE_WIDGET)

typedef enum { PROP_HEAD = 1, N_PROPERTIES } HwdoutHeadEditorProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
hwdout_head_editor_dispose(GObject *gobject) {
    HwdoutHeadEditor *self = HWDOUT_HEAD_EDITOR(gobject);

    gtk_widget_dispose_template(GTK_WIDGET(self), HWDOUT_TYPE_HEAD_EDITOR);

    g_clear_object(&self->head);

    G_OBJECT_CLASS(hwdout_head_editor_parent_class)->dispose(gobject);
}

static void
hwdout_head_editor_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutHeadEditor *self = HWDOUT_HEAD_EDITOR(gobject);

    switch ((HwdoutHeadEditorProperty)property_id) {
    case PROP_HEAD:
        hwdout_head_editor_set_head(self, g_value_get_object(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_head_editor_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutHeadEditor *self = HWDOUT_HEAD_EDITOR(gobject);

    switch ((HwdoutHeadEditorProperty)property_id) {
    case PROP_HEAD:
        g_value_set_object(value, hwdout_head_editor_get_head(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_head_editor_class_init(HwdoutHeadEditorClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    object_class->dispose = hwdout_head_editor_dispose;
    object_class->set_property = hwdout_head_editor_set_property;
    object_class->get_property = hwdout_head_editor_get_property;

    properties[PROP_HEAD] = g_param_spec_object(
        "head", "Head", "Head configuration object that this editor is modifying",
        HWDOUT_TYPE_CONFIGURATION_HEAD, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_template_from_resource(
        widget_class, "/com/bwhmather/hwdout/ui/hwdout-head-editor.ui"
    );
}

static void
hwdout_head_editor_init(HwdoutHeadEditor *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
}

HwdoutHeadEditor *
hwdout_head_editor_new(void) {
    return g_object_new(HWDOUT_TYPE_HEAD_EDITOR, NULL);
}

void
hwdout_head_editor_set_head(HwdoutHeadEditor *self, HwdoutConfigurationHead *head) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (head == self->head) {
        return;
    }

    g_clear_object(&self->head);
    self->head = head;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD]);
}

HwdoutConfigurationHead *
hwdout_head_editor_get_head(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), NULL);

    return self->head;
}
