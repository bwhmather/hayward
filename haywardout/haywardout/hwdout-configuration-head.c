#include "hwdout-configuration-head.h"

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

#include "hwdout-configuration.h"
#include "hwdout-head.h"
#include "hwdout-manager.h"
#include "hwdout-mode.h"
#include "hwdout-transform.h"
#include "hwdout-util.h"

struct _HwdoutConfigurationHead {
    GObject parent_instance;

    GWeakRef configuration;

    HwdoutManager *manager;
    HwdoutHead *head;

    gboolean is_enabled;

    HwdoutMode *mode;
    gint width;
    gint height;
    gint refresh;

    gint x;
    gint y;

    HwdoutTransform transform;
    wl_fixed_t scale;
};

G_DEFINE_TYPE(HwdoutConfigurationHead, hwdout_configuration_head, G_TYPE_OBJECT)

typedef enum {
    PROP_CONFIGURATION = 1,
    PROP_MANAGER,
    PROP_HEAD,
    PROP_IS_ENABLED,
    PROP_MODE,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_REFRESH,
    PROP_X,
    PROP_Y,
    PROP_TRANSFORM,
    PROP_SCALE,
    N_PROPERTIES
} HwdoutConfigurationHeadProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
hwdout_configuration_head_constructed(GObject *gobject) {
    HwdoutConfigurationHead *self = HWDOUT_CONFIGURATION_HEAD(gobject);
    HwdoutConfiguration *configuration;
    HwdoutHead *head;

    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    G_OBJECT_CLASS(hwdout_configuration_head_parent_class)->constructed(gobject);

    configuration = hwdout_configuration_head_get_configuration(self);
    g_return_if_fail(HWDOUT_IS_CONFIGURATION(configuration));

    self->manager = hwdout_configuration_get_manager(configuration);
    g_return_if_fail(HWDOUT_IS_MANAGER(self->manager));

    head = self->head;
    g_return_if_fail(HWDOUT_IS_HEAD(head));
    g_return_if_fail(hwdout_head_get_manager(head) == self->manager);

    self->is_enabled = hwdout_head_get_is_enabled(head);
    self->mode = hwdout_head_get_current_mode(head);
    if (self->mode) {
        self->width = hwdout_mode_get_width(self->mode);
        self->height = hwdout_mode_get_height(self->mode);
        self->refresh = hwdout_mode_get_refresh(self->mode);
    }

    self->x = hwdout_head_get_x(head);
    self->y = hwdout_head_get_y(head);

    self->transform = hwdout_head_get_transform(head);
    self->scale = hwdout_head_get_scale(head);

    g_object_unref(G_OBJECT(configuration));
}

static void
hwdout_configuration_head_dispose(GObject *gobject) {
    HwdoutConfigurationHead *self = HWDOUT_CONFIGURATION_HEAD(gobject);

    g_weak_ref_clear(&self->configuration);

    g_clear_object(&self->manager);
    g_clear_object(&self->head);

    g_clear_object(&self->mode);

    G_OBJECT_CLASS(hwdout_configuration_head_parent_class)->dispose(gobject);
}

static void
hwdout_configuration_head_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutConfigurationHead *self = HWDOUT_CONFIGURATION_HEAD(gobject);

    switch ((HwdoutConfigurationHeadProperty)property_id) {
    case PROP_CONFIGURATION:
        g_weak_ref_set(&self->configuration, g_value_get_object(value));
        break;

    case PROP_HEAD:
        g_clear_object(&self->head);
        self->head = g_value_dup_object(value);
        break;

    case PROP_IS_ENABLED:
        hwdout_configuration_head_set_is_enabled(self, g_value_get_boolean(value));
        break;

    case PROP_MODE:
        hwdout_configuration_head_set_mode(self, g_value_get_object(value));
        break;

    case PROP_WIDTH:
        hwdout_configuration_head_set_width(self, g_value_get_int(value));
        break;

    case PROP_HEIGHT:
        hwdout_configuration_head_set_height(self, g_value_get_int(value));
        break;

    case PROP_REFRESH:
        hwdout_configuration_head_set_refresh(self, g_value_get_int(value));
        break;

    case PROP_X:
        hwdout_configuration_head_set_x(self, g_value_get_int(value));
        break;

    case PROP_Y:
        hwdout_configuration_head_set_y(self, g_value_get_int(value));
        break;

    case PROP_TRANSFORM:
        hwdout_configuration_head_set_transform(self, g_value_get_enum(value));
        break;

    case PROP_SCALE:
        hwdout_configuration_head_set_scale(self, g_value_get_double(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_configuration_head_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutConfigurationHead *self = HWDOUT_CONFIGURATION_HEAD(gobject);

    switch ((HwdoutConfigurationHeadProperty)property_id) {
    case PROP_CONFIGURATION:
        g_value_take_object(value, hwdout_configuration_head_get_configuration(self));
        break;

    case PROP_MANAGER:
        g_value_set_object(value, hwdout_configuration_head_get_manager(self));
        break;

    case PROP_HEAD:
        g_value_set_object(value, hwdout_configuration_head_get_head(self));
        break;

    case PROP_IS_ENABLED:
        g_value_set_boolean(value, hwdout_configuration_head_get_is_enabled(self));
        break;

    case PROP_MODE:
        g_value_set_object(value, hwdout_configuration_head_get_mode(self));
        break;

    case PROP_WIDTH:
        g_value_set_int(value, hwdout_configuration_head_get_width(self));
        break;

    case PROP_HEIGHT:
        g_value_set_int(value, hwdout_configuration_head_get_height(self));
        break;

    case PROP_REFRESH:
        g_value_set_int(value, hwdout_configuration_head_get_refresh(self));
        break;

    case PROP_X:
        g_value_set_int(value, hwdout_configuration_head_get_x(self));
        break;

    case PROP_Y:
        g_value_set_int(value, hwdout_configuration_head_get_y(self));
        break;

    case PROP_TRANSFORM:
        g_value_set_enum(value, hwdout_configuration_head_get_transform(self));
        break;

    case PROP_SCALE:
        g_value_set_double(value, hwdout_configuration_head_get_scale(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_configuration_head_class_init(HwdoutConfigurationHeadClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_configuration_head_constructed;
    object_class->dispose = hwdout_configuration_head_dispose;
    object_class->set_property = hwdout_configuration_head_set_property;
    object_class->get_property = hwdout_configuration_head_get_property;

    properties[PROP_CONFIGURATION] = g_param_spec_object(
        "configuration", "Output configuration",
        "The output configuration object that owns this head", HWDOUT_TYPE_CONFIGURATION,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    );

    /**
     * HwdoutConfiguration:manager: (attributes org.gtk.Property.get=hwdout_configuration_get_manager)
     *
     * Reference to the output manager which owns the head which this object
     * configures.
     */
    properties[PROP_MANAGER] = g_param_spec_object(
        "manager", "Output manager",
        "Reference to the output manager which owns the head which this object configures",
        HWDOUT_TYPE_MANAGER, G_PARAM_READABLE
    );

    properties[PROP_HEAD] = g_param_spec_object(
        "head", "Output head", "Reference to the specific head object which this object configures",
        HWDOUT_TYPE_HEAD, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    );

    properties[PROP_IS_ENABLED] = g_param_spec_boolean(
        "is-enabled", "Enabled", "Whether or not this output should be switched on",
        FALSE, // Default.
        G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
    );

    properties[PROP_MODE] = g_param_spec_object(
        "mode", "Output mode", "The output mode to set when this configuration is applied",
        HWDOUT_TYPE_MODE, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
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
        HWDOUT_TYPE_TRANSFORM, HWDOUT_TRANSFORM_NORMAL, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY
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
hwdout_configuration_head_init(HwdoutConfigurationHead *self) {}

HwdoutConfigurationHead *
hwdout_configuration_head_new(HwdoutConfiguration *configuration, HwdoutHead *head) {
    return g_object_new(
        HWDOUT_TYPE_CONFIGURATION_HEAD, "configuration", configuration, "head", head, NULL
    );
}

/**
 * hwdout_configuration_head_get_manager: (attributes org.gtk.Method.get_property=manager)
 * @self: a `HwdoutConfigurationHead`
 *
 * Gets the output manager that the head that this object configures is owned
 * by.
 *
 * Returns: (transfer none): The owning output manager.
 */
HwdoutManager *
hwdout_configuration_head_get_manager(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION(self), NULL);

    return self->manager;
}

/**
 * hwdout_configuration_head_get_configuration: (attributes org.gtk.Method.get_property=configuration)
 * @self: a `HwdoutConfigurationHead`
 *
 * Gets the output configuration that this object is part of.
 *
 * Returns: (transfer full): The containing configuration.
 */
HwdoutConfiguration *
hwdout_configuration_head_get_configuration(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), NULL);

    return HWDOUT_CONFIGURATION(g_weak_ref_get(&self->configuration));
}

/**
 * hwdout_configuration_head_get_head: (attributes org.gik.Method.get_property=head)
 * @self: a `HwdoutConfigurationHead`
 *
 * Returns: (transfer none): The corresponding head.
 */
HwdoutHead *
hwdout_configuration_head_get_head(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), NULL);

    return self->head;
}

void
hwdout_configuration_head_set_is_enabled(HwdoutConfigurationHead *self, gboolean is_enabled) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (is_enabled == self->is_enabled) {
        return;
    }

    self->is_enabled = is_enabled;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_IS_ENABLED]);
}

gboolean
hwdout_configuration_head_get_is_enabled(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), FALSE);

    return self->is_enabled;
}

void
hwdout_configuration_head_set_mode(HwdoutConfigurationHead *self, HwdoutMode *mode) {
    gint width;
    gint height;
    gint refresh;

    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (mode == self->mode) {
        return;
    }

    g_clear_object(&self->mode);
    self->mode = mode;

    if (mode != NULL) {
        g_object_ref(G_OBJECT(mode));

        width = hwdout_mode_get_width(mode);
        if (self->width != width) {
            self->width = width;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_WIDTH]);
        }

        height = hwdout_mode_get_height(mode);
        if (self->height != height) {
            self->height = height;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEIGHT]);
        }

        refresh = hwdout_mode_get_refresh(mode);
        if (self->refresh != refresh) {
            self->refresh = refresh;
            g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_REFRESH]);
        }
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
}

HwdoutMode *
hwdout_configuration_head_get_mode(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), NULL);

    return self->mode;
}

void
hwdout_configuration_head_set_width(HwdoutConfigurationHead *self, gint width) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (width == self->width) {
        return;
    }

    self->width = width;

    if (self->mode != NULL) {
        g_clear_object(&self->mode);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_WIDTH]);
}

gint
hwdout_configuration_head_get_width(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), 0);

    return self->width;
}

void
hwdout_configuration_head_set_height(HwdoutConfigurationHead *self, gint height) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (height == self->height) {
        return;
    }

    self->height = height;

    if (self->mode != NULL) {
        g_clear_object(&self->mode);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_HEIGHT]);
}

gint
hwdout_configuration_head_get_height(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), 0);

    return self->height;
}

void
hwdout_configuration_head_set_refresh(HwdoutConfigurationHead *self, gint refresh) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (refresh == self->refresh) {
        return;
    }

    self->refresh = refresh;

    if (self->mode != NULL) {
        g_clear_object(&self->mode);
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODE]);
    }

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_REFRESH]);
}

gint
hwdout_configuration_head_get_refresh(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), 0);

    return self->refresh;
}

void
hwdout_configuration_head_set_x(HwdoutConfigurationHead *self, gint x) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (x == self->x) {
        return;
    }

    self->x = x;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_X]);
}

gint
hwdout_configuration_head_get_x(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), 0);

    return self->x;
}

void
hwdout_configuration_head_set_y(HwdoutConfigurationHead *self, gint y) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (y == self->y) {
        return;
    }

    self->y = y;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_Y]);
}

gint
hwdout_configuration_head_get_y(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), 0);

    return self->y;
}

void
hwdout_configuration_head_set_transform(HwdoutConfigurationHead *self, HwdoutTransform transform) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (transform == self->transform) {
        return;
    }

    self->transform = transform;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_TRANSFORM]);
}

HwdoutTransform
hwdout_configuration_head_get_transform(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), HWDOUT_TRANSFORM_NORMAL);

    return self->transform;
}

void
hwdout_configuration_head_set_scale(HwdoutConfigurationHead *self, double scale) {
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self));

    if (scale == self->scale) {
        return;
    }

    self->scale = scale;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SCALE]);
}

double
hwdout_configuration_head_get_scale(HwdoutConfigurationHead *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(self), 0.0);

    return self->scale;
}
