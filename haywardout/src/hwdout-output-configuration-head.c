#include "hwdout-output-configuration-head.h"

#include "hwdout-output-configuration.h"
#include "hwdout-output-head.h"
#include "hwdout-output-manager.h"
#include "hwdout-output-mode.h"
#include "hwdout-output-transform.h"
#include "hwdout-util.h"

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _HwdoutOutputConfigurationHead {
    GObject parent_instance;

    GWeakRef output_configuration;

    HwdoutOutputManager *output_manager;
    HwdoutOutputHead *output_head;

    HwdoutOutputMode *output_mode;
    gint width;
    gint height;
    gint refresh;

    gint x;
    gint y;

    HwdoutOutputTransform transform;
    wl_fixed_t scale;
};

G_DEFINE_TYPE(HwdoutOutputConfigurationHead, hwdout_output_configuration_head, G_TYPE_OBJECT)

typedef enum {
    PROP_OUTPUT_CONFIGURATION = 1,
    PROP_OUTPUT_MANAGER,
    PROP_OUTPUT_HEAD,
    PROP_MODE,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_REFRESH,
    PROP_X,
    PROP_Y,
    PROP_TRANSFORM,
    PROP_SCALE,
    N_PROPERTIES
} HwdoutOutputConfigurationHeadProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
hwdout_output_configuration_head_constructed(GObject *gobject) {
    HwdoutOutputConfigurationHead *self = HWDOUT_OUTPUT_CONFIGURATION_HEAD(gobject);
    HwdoutOutputConfiguration *configuration;
    HwdoutOutputHead *head;

    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    G_OBJECT_CLASS(hwdout_output_configuration_head_parent_class)->constructed(gobject);

    configuration = hwdout_output_configuration_head_get_output_configuration(self);
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION(configuration));

    self->output_manager = hwdout_output_configuration_get_output_manager(configuration);
    g_return_if_fail(HWDOUT_IS_OUTPUT_MANAGER(self->output_manager));

    head = self->output_head;
    g_return_if_fail(HWDOUT_IS_OUTPUT_HEAD(head));
    g_return_if_fail(hwdout_output_head_get_output_manager(head) == self->output_manager);

    self->output_mode = hwdout_output_head_get_current_mode(head);
    self->width = hwdout_output_mode_get_width(self->output_mode);
    self->height = hwdout_output_mode_get_height(self->output_mode);
    self->refresh = hwdout_output_mode_get_refresh(self->output_mode);

    self->x = hwdout_output_head_get_x(head);
    self->y = hwdout_output_head_get_y(head);

    self->transform = hwdout_output_head_get_transform(head);
    self->scale = hwdout_output_head_get_scale(head);

    g_object_unref(G_OBJECT(configuration));
}

static void
hwdout_output_configuration_head_dispose(GObject *gobject) {
    HwdoutOutputConfigurationHead *self = HWDOUT_OUTPUT_CONFIGURATION_HEAD(gobject);

    g_weak_ref_clear(&self->output_configuration);

    g_clear_object(&self->output_manager);
    g_clear_object(&self->output_head);

    g_clear_object(&self->output_mode);

    G_OBJECT_CLASS(hwdout_output_configuration_head_parent_class)->dispose(gobject);
}

static void
hwdout_output_configuration_head_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutOutputConfigurationHead *self = HWDOUT_OUTPUT_CONFIGURATION_HEAD(gobject);

    switch ((HwdoutOutputConfigurationHeadProperty)property_id) {
    case PROP_OUTPUT_CONFIGURATION:
        g_weak_ref_set(&self->output_configuration, g_value_get_object(value));
        break;

    case PROP_OUTPUT_HEAD:
        g_clear_object(&self->output_head);
        self->output_head = g_value_dup_object(value);
        break;

    case PROP_MODE:
        hwdout_output_configuration_head_set_mode(self, g_value_get_object(value));
        break;

    case PROP_WIDTH:
        hwdout_output_configuration_head_set_width(self, g_value_get_int(value));
        break;

    case PROP_HEIGHT:
        hwdout_output_configuration_head_set_height(self, g_value_get_int(value));
        break;

    case PROP_REFRESH:
        hwdout_output_configuration_head_set_refresh(self, g_value_get_int(value));
        break;

    case PROP_X:
        hwdout_output_configuration_head_set_x(self, g_value_get_int(value));
        break;

    case PROP_Y:
        hwdout_output_configuration_head_set_y(self, g_value_get_int(value));
        break;

    case PROP_TRANSFORM:
        hwdout_output_configuration_head_set_transform(self, g_value_get_enum(value));
        break;

    case PROP_SCALE:
        hwdout_output_configuration_head_set_scale(self, g_value_get_double(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_configuration_head_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutOutputConfigurationHead *self = HWDOUT_OUTPUT_CONFIGURATION_HEAD(gobject);

    switch ((HwdoutOutputConfigurationHeadProperty)property_id) {
    case PROP_OUTPUT_CONFIGURATION:
        g_value_take_object(value, hwdout_output_configuration_head_get_output_configuration(self));
        break;

    case PROP_OUTPUT_MANAGER:
        g_value_set_object(value, hwdout_output_configuration_head_get_output_manager(self));
        break;

    case PROP_OUTPUT_HEAD:
        g_value_set_object(value, hwdout_output_configuration_head_get_output_head(self));
        break;

    case PROP_MODE:
        g_value_set_object(value, hwdout_output_configuration_head_get_mode(self));
        break;

    case PROP_WIDTH:
        g_value_set_int(value, hwdout_output_configuration_head_get_width(self));
        break;

    case PROP_HEIGHT:
        g_value_set_int(value, hwdout_output_configuration_head_get_height(self));
        break;

    case PROP_REFRESH:
        g_value_set_int(value, hwdout_output_configuration_head_get_refresh(self));
        break;

    case PROP_X:
        g_value_set_int(value, hwdout_output_configuration_head_get_x(self));
        break;

    case PROP_Y:
        g_value_set_int(value, hwdout_output_configuration_head_get_y(self));
        break;

    case PROP_TRANSFORM:
        g_value_set_enum(value, hwdout_output_configuration_head_get_transform(self));
        break;

    case PROP_SCALE:
        g_value_set_double(value, hwdout_output_configuration_head_get_scale(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_configuration_head_class_init(HwdoutOutputConfigurationHeadClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_output_configuration_head_constructed;
    object_class->dispose = hwdout_output_configuration_head_dispose;
    object_class->set_property = hwdout_output_configuration_head_set_property;
    object_class->get_property = hwdout_output_configuration_head_get_property;

    properties[PROP_OUTPUT_CONFIGURATION] = g_param_spec_object(
        "output-configuration", "Output configuration",
        "The output configuration object that owns this head", HWDOUT_TYPE_OUTPUT_CONFIGURATION,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    );

    /**
     * HwdoutOutputConfiguration:output-manager: (attributes org.gtk.Property.get=hwdout_output_configuration_get_output_manager)
     *
     * Reference to the output manager which owns the head which this object
     * configures.
     */
    properties[PROP_OUTPUT_MANAGER] = g_param_spec_object(
        "output-manager", "Output manager",
        "Reference to the output manager which owns the head which this object configures",
        HWDOUT_TYPE_OUTPUT_MANAGER, G_PARAM_READABLE
    );

    properties[PROP_OUTPUT_HEAD] = g_param_spec_object(
        "output-head", "Output head",
        "Reference to the specific head object which this object configures",
        HWDOUT_TYPE_OUTPUT_HEAD, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    );

    properties[PROP_MODE] = g_param_spec_object(
        "output-mode", "Output mode", "The output mode to set when this configuration is applied",
        HWDOUT_TYPE_OUTPUT_MODE, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_WIDTH] = g_param_spec_int(
        "width", "Width", "Horizontal size of the output buffer in pixels",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );
    properties[PROP_HEIGHT] = g_param_spec_int(
        "height", "Height", "Vertical size of the output buffer in pixels",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_REFRESH] = g_param_spec_int(
        "refresh", "Refresh rate", "Refresh rate in mHz or zero.",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_X] = g_param_spec_int(
        "x", "X", "Offset of leftmost edge of the output in layout coordinates",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );
    properties[PROP_Y] = g_param_spec_int(
        "y", "Y", "Offset of topmost edge of the output in layout coordinates",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_TRANSFORM] = g_param_spec_enum(
        "transform", "Transform", "Describes how the display is currently rotated and or flipped",
        HWDOUT_TYPE_OUTPUT_TRANSFORM, HWDOUT_OUTPUT_TRANSFORM_NORMAL,
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_SCALE] = g_param_spec_double(
        "scale", "Scale",
        "Amount by which layout coordinates should be multiplied to map to physical coordinates",
        0.000001, // Minimum.
        100.0,    // Maximum,
        1.0,      // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

static void
hwdout_output_configuration_head_init(HwdoutOutputConfigurationHead *self) {}

HwdoutOutputConfigurationHead *
hwdout_output_configuration_head_new(
    HwdoutOutputConfiguration *output_configuration, HwdoutOutputHead *output_head
) {
    return g_object_new(
        HWDOUT_TYPE_OUTPUT_CONFIGURATION_HEAD, "output-configuration", output_configuration,
        "output-head", output_head, NULL
    );
}

/**
 * hwdout_output_configuration_head_get_output_manager: (attributes org.gtk.Method.get_property=output-manager)
 * @self: a `HwdoutOutputConfigurationHead`
 *
 * Gets the output manager that the head that this object configures is owned
 * by.
 *
 * Returns: (transfer none): The owning output manager.
 */
HwdoutOutputManager *
hwdout_output_configuration_head_get_output_manager(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION(self), NULL);

    return self->output_manager;
}

/**
 * hwdout_output_configuration_head_get_output_configuration: (attributes org.gtk.Method.get_property=output-configuration)
 * @self: a `HwdoutOutputConfigurationHead`
 *
 * Gets the output configuration that this object is part of.
 *
 * Returns: (transfer full): The containing configuration.
 */
HwdoutOutputConfiguration *
hwdout_output_configuration_head_get_output_configuration(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), NULL);

    return HWDOUT_OUTPUT_CONFIGURATION(g_weak_ref_get(&self->output_configuration));
}

/**
 * hwdout_output_configuration_head_get_output_head: (attributes org.gik.Method.get_property=output-head)
 * @self: a `HwdoutOutputConfigurationHead`
 *
 * Returns: (transfer none): The corresponding head.
 */
HwdoutOutputHead *
hwdout_output_configuration_head_get_output_head(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), NULL);

    return self->output_head;
}

void
hwdout_output_configuration_head_set_mode(
    HwdoutOutputConfigurationHead *self, HwdoutOutputMode *mode
) {
    gint width;
    gint height;
    gint refresh;

    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    if (mode == self->output_mode) {
        return;
    }

    g_clear_object(&self->output_mode);
    self->output_mode = mode;

    if (mode != NULL) {
        g_object_ref(G_OBJECT(mode));

        width = hwdout_output_mode_get_width(mode);
        if (self->width != width) {
            self->width = width;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_WIDTH]);
        }

        height = hwdout_output_mode_get_height(mode);
        if (self->height != height) {
            self->height = height;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEIGHT]);
        }

        refresh = hwdout_output_mode_get_refresh(mode);
        if (self->refresh != refresh) {
            self->refresh = refresh;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_REFRESH]);
        }
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
}

HwdoutOutputMode *
hwdout_output_configuration_head_get_mode(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), NULL);

    return self->output_mode;
}

void
hwdout_output_configuration_head_set_width(HwdoutOutputConfigurationHead *self, gint width) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->width = width;

    if (self->output_mode != NULL) {
        g_clear_object(&self->output_mode);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_WIDTH]);
}

gint
hwdout_output_configuration_head_get_width(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), 0);

    return self->width;
}

void
hwdout_output_configuration_head_set_height(HwdoutOutputConfigurationHead *self, gint height) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->height = height;

    if (self->output_mode != NULL) {
        g_clear_object(&self->output_mode);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEIGHT]);
}

gint
hwdout_output_configuration_head_get_height(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), 0);

    return self->height;
}

void
hwdout_output_configuration_head_set_refresh(HwdoutOutputConfigurationHead *self, gint refresh) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->refresh = refresh;

    if (self->output_mode != NULL) {
        g_clear_object(&self->output_mode);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_REFRESH]);
}

gint
hwdout_output_configuration_head_get_refresh(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), 0);

    return self->refresh;
}

void
hwdout_output_configuration_head_set_x(HwdoutOutputConfigurationHead *self, gint x) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->x = x;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_X]);
}

gint
hwdout_output_configuration_head_get_x(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), 0);

    return self->x;
}

void
hwdout_output_configuration_head_set_y(HwdoutOutputConfigurationHead *self, gint y) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->y = y;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_Y]);
}

gint
hwdout_output_configuration_head_get_y(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), 0);

    return self->y;
}

void
hwdout_output_configuration_head_set_transform(
    HwdoutOutputConfigurationHead *self, HwdoutOutputTransform transform
) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->transform = transform;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_TRANSFORM]);
}

HwdoutOutputTransform
hwdout_output_configuration_head_get_transform(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), HWDOUT_OUTPUT_TRANSFORM_NORMAL);

    return self->transform;
}

void
hwdout_output_configuration_head_set_scale(HwdoutOutputConfigurationHead *self, double scale) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self));

    self->scale = scale;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SCALE]);
}

double
hwdout_output_configuration_head_get_scale(HwdoutOutputConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION_HEAD(self), 0.0);

    return self->scale;
}
