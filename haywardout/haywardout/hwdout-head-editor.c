#include "hwdout-head-editor.h"

#include <gtk/gtk.h>

#include "hwdout-configuration-head.h"
#include "hwdout-mode.h"

struct _HwdoutHeadEditor {
    GtkWidget parent_instance;

    GtkSpinButton *scale_input;

    HwdoutConfigurationHead *head;

    gchar *name;
    gchar *description;

    gboolean is_enabled;

    GListModel *modes;
    HwdoutMode *mode;

    HwdoutTransform transform;
    gdouble scale;

    GBinding *name_binding;
    GBinding *description_binding;
    GBinding *is_enabled_binding;
    GBinding *modes_binding;
    GBinding *mode_binding;
    GBinding *transform_binding;
    GBinding *scale_binding;
};

G_DEFINE_TYPE(HwdoutHeadEditor, hwdout_head_editor, GTK_TYPE_WIDGET)

typedef enum {
    PROP_HEAD = 1,
    PROP_HEAD_NAME,
    PROP_HEAD_DESCRIPTION,
    PROP_HEAD_IS_ENABLED,
    PROP_HEAD_MODES,
    PROP_HEAD_MODE,
    PROP_HEAD_TRANSFORM,
    PROP_HEAD_SCALE,
    N_PROPERTIES,
} HwdoutHeadEditorProperty;

static GParamSpec *properties[N_PROPERTIES];

static gchararray
describe_mode(HwdoutMode *mode) {
    g_return_val_if_fail(HWDOUT_IS_MODE(mode), NULL);

    return g_strdup_printf(
        "%ix%i (%iHz)", hwdout_mode_get_width(mode), hwdout_mode_get_height(mode),
        hwdout_mode_get_refresh(mode) / 1000
    );
}

static gboolean
set_is_enabled(GtkSwitch *enabled_switch, gboolean is_enabled, gpointer user_data) {
    HwdoutHeadEditor *self = HWDOUT_HEAD_EDITOR(user_data);

    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), TRUE);

    if (self->head == NULL) {
        return TRUE;
    }

    hwdout_configuration_head_set_is_enabled(self->head, is_enabled);
    gtk_switch_set_state(enabled_switch, is_enabled);

    return TRUE;
}

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

    case PROP_HEAD_NAME:
        hwdout_head_editor_set_head_name(self, g_value_get_string(value));
        break;

    case PROP_HEAD_DESCRIPTION:
        hwdout_head_editor_set_head_description(self, g_value_get_string(value));
        break;

    case PROP_HEAD_IS_ENABLED:
        hwdout_head_editor_set_head_is_enabled(self, g_value_get_boolean(value));
        break;

    case PROP_HEAD_MODES:
        hwdout_head_editor_set_head_modes(self, g_value_get_object(value));
        break;

    case PROP_HEAD_MODE:
        hwdout_head_editor_set_head_mode(self, g_value_get_object(value));
        break;

    case PROP_HEAD_TRANSFORM:
        hwdout_head_editor_set_head_transform(self, g_value_get_enum(value));
        break;

    case PROP_HEAD_SCALE:
        hwdout_head_editor_set_head_scale(self, g_value_get_double(value));
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

    case PROP_HEAD_NAME:
        g_value_set_string(value, hwdout_head_editor_get_head_name(self));
        break;

    case PROP_HEAD_DESCRIPTION:
        g_value_set_string(value, hwdout_head_editor_get_head_description(self));
        break;

    case PROP_HEAD_IS_ENABLED:
        g_value_set_boolean(value, hwdout_head_editor_get_head_is_enabled(self));
        break;

    case PROP_HEAD_MODES:
        g_value_set_object(value, hwdout_head_editor_get_head_modes(self));
        break;

    case PROP_HEAD_MODE:
        g_value_set_object(value, hwdout_head_editor_get_head_mode(self));
        break;

    case PROP_HEAD_TRANSFORM:
        g_value_set_enum(value, hwdout_head_editor_get_head_transform(self));
        break;

    case PROP_HEAD_SCALE:
        g_value_set_double(value, hwdout_head_editor_get_head_scale(self));
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

    properties[PROP_HEAD_NAME] = g_param_spec_string(
        "head-name", "Head name", "Short unique name that identifies the output",
        NULL, // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_HEAD_DESCRIPTION] = g_param_spec_string(
        "head-description", "Head description", "Human readable description of the output",
        NULL, // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_HEAD_IS_ENABLED] = g_param_spec_boolean(
        "head-is-enabled", "Head is enabled",
        "Whether or not the compositor should render to this output",
        FALSE, // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_HEAD_MODE] = g_param_spec_object(
        "head-mode", "Mode",
        "Reference to the mode object representing the desired mode", HWDOUT_TYPE_MODE,
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_HEAD_MODES] = g_param_spec_object(
        "head-modes", "Supported modes", "List of all modes that this output explicitly supports",
        G_TYPE_LIST_MODEL, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_HEAD_TRANSFORM] = g_param_spec_enum(
        "head-transform", "Head transform",
        "Describes how the display should be rotated and or flipped", HWDOUT_TYPE_TRANSFORM,
        HWDOUT_TRANSFORM_NORMAL, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_HEAD_SCALE] = g_param_spec_double(
        "head-scale", "Head scale",
        "Amount by which layout coordinates should be multiplied to map to physical coordinates",
        0.000001, // Minimum.
        100.0,    // Maximum,
        1.0,      // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    gtk_widget_class_set_layout_manager_type(widget_class, GTK_TYPE_BIN_LAYOUT);
    gtk_widget_class_set_template_from_resource(
        widget_class, "/com/bwhmather/hwdout/ui/hwdout-head-editor.ui"
    );
    gtk_widget_class_bind_template_callback(widget_class, describe_mode);
    gtk_widget_class_bind_template_callback(widget_class, set_is_enabled);
    gtk_widget_class_bind_template_child(widget_class, HwdoutHeadEditor, scale_input);
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
hwdout_head_editor_set_head(HwdoutHeadEditor *self, HwdoutConfigurationHead *config_head) {
    GtkAdjustment *scale_adjustment;
    HwdoutHead *head;

    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (config_head == self->head) {
        return;
    }

    g_clear_object(&self->head);
    self->head = config_head;

    head = hwdout_configuration_head_get_head(config_head);

    g_clear_pointer(&self->name_binding, g_binding_unbind);
    self->name_binding =
        g_object_bind_property(head, "name", self, "head-name", G_BINDING_SYNC_CREATE);

    g_clear_pointer(&self->description_binding, g_binding_unbind);
    self->description_binding = g_object_bind_property(
        head, "description", self, "head-description", G_BINDING_SYNC_CREATE
    );

    g_clear_pointer(&self->is_enabled_binding, g_binding_unbind);
    self->is_enabled_binding = g_object_bind_property(
        config_head, "is-enabled", self, "head-is-enabled",
        G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE
    );

    g_clear_pointer(&self->modes_binding, g_binding_unbind);
    self->modes_binding =
        g_object_bind_property(head, "modes", self, "head-modes", G_BINDING_SYNC_CREATE);

    g_clear_pointer(&self->mode_binding, g_binding_unbind);
    self->mode_binding = g_object_bind_property(
        config_head, "mode", self, "head-mode",
        G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE
    );

    g_clear_pointer(&self->transform_binding, g_binding_unbind);
    self->transform_binding = g_object_bind_property(
        config_head, "transform", self, "head-transform",
        G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE
    );

    g_clear_pointer(&self->scale_binding, g_binding_unbind);
    self->scale_binding = g_object_bind_property(
        config_head, "scale", self, "head-scale", G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE
    );

    scale_adjustment = gtk_adjustment_new(
        hwdout_configuration_head_get_scale(config_head), 0.25, 4.0, 0.125, 0.25, 0.0
    );
    gtk_spin_button_configure(self->scale_input, scale_adjustment, 0.125, 4);

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD]);
}

HwdoutConfigurationHead *
hwdout_head_editor_get_head(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), NULL);

    return self->head;
}

void
hwdout_head_editor_set_head_name(HwdoutHeadEditor *self, const gchar *name) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (g_strcmp0(name, self->name) == 0) {
        return;
    }
    g_clear_pointer(&self->name, g_free);
    self->name = g_strdup(name);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_NAME]);
}

const gchar *
hwdout_head_editor_get_head_name(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), FALSE);

    return self->name;
}

void
hwdout_head_editor_set_head_description(HwdoutHeadEditor *self, const gchar *description) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (g_strcmp0(description, self->description) == 0) {
        return;
    }
    g_clear_pointer(&self->description, g_free);
    self->description = g_strdup(description);
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_DESCRIPTION]);
}

const gchar *
hwdout_head_editor_get_head_description(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), FALSE);

    return self->description;
}

void
hwdout_head_editor_set_head_is_enabled(HwdoutHeadEditor *self, gboolean is_enabled) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (is_enabled == self->is_enabled) {
        return;
    }
    self->is_enabled = is_enabled;
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_IS_ENABLED]);
}

gboolean
hwdout_head_editor_get_head_is_enabled(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), FALSE);

    return self->is_enabled;
}

void
hwdout_head_editor_set_head_mode(HwdoutHeadEditor *self, HwdoutMode *mode) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (mode == self->mode) {
        g_clear_object(&mode);
        return;
    }
    g_clear_object(&self->mode);
    self->mode = mode;
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_MODE]);
}

HwdoutMode *
hwdout_head_editor_get_head_mode(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), NULL);

    return self->mode;
}

void
hwdout_head_editor_set_head_modes(HwdoutHeadEditor *self, GListModel *modes) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (modes == self->modes) {
        g_clear_object(&modes);
        return;
    }
    g_clear_object(&self->modes);
    self->modes = modes;
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_MODES]);
}

GListModel *
hwdout_head_editor_get_head_modes(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), NULL);

    return self->modes;
}

void
hwdout_head_editor_set_head_transform(HwdoutHeadEditor *self, HwdoutTransform transform) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (transform == self->transform) {
        return;
    }
    self->transform = transform;
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_TRANSFORM]);
}

HwdoutTransform
hwdout_head_editor_get_head_transform(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), HWDOUT_TRANSFORM_NORMAL);

    return self->transform;
}

void
hwdout_head_editor_set_head_scale(HwdoutHeadEditor *self, double scale) {
    g_return_if_fail(HWDOUT_IS_HEAD_EDITOR(self));

    if (scale == self->scale) {
        return;
    }
    self->scale = scale;
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEAD_SCALE]);
}

double
hwdout_head_editor_get_head_scale(HwdoutHeadEditor *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD_EDITOR(self), 0.0);

    return self->scale;
}
