#include "hwdout-output-configuration.h"

#include "hwdout-output-configuration-head.h"
#include "hwdout-output-head.h"
#include "hwdout-output-manager.h"
#include "hwdout-util.h"

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _HwdoutOutputConfiguration {
    GObject parent_instance;

    struct zwlr_output_configuration_v1 *wlr_output_configuration;

    HwdoutOutputManager *output_manager;
    uint32_t serial;

    GListStore *heads; // `HwdoutOutputConfigurationHead`.

    gboolean finished;
};

G_DEFINE_TYPE(HwdoutOutputConfiguration, hwdout_output_configuration, G_TYPE_OBJECT)

typedef enum {
    PROP_OUTPUT_MANAGER = 1,
    PROP_SERIAL,
    N_PROPERTIES
} HwdoutOutputConfigurationProperty;

static GParamSpec *properties[N_PROPERTIES];

typedef enum {
    SIGNAL_SUCCEEDED = 1,
    SIGNAL_FAILED,
    SIGNAL_CANCELLED,
    N_SIGNALS,
} HwdoutOutputConfigurationSignal;

static guint signals[N_SIGNALS] = {0};

static void
handle_output_manager_done(HwdoutOutputManager *manager, uint32_t serial, void *data) {
    HwdoutOutputConfiguration *self = HWDOUT_OUTPUT_CONFIGURATION(data);

    if (self->finished) {
        return;
    }

    self->finished = true;
    g_clear_pointer(&self->wlr_output_configuration, zwlr_output_configuration_v1_destroy);
    g_signal_emit(self, signals[SIGNAL_CANCELLED], 0);
}

static void
hwdout_output_configuration_constructed(GObject *gobject) {
    HwdoutOutputConfiguration *self = HWDOUT_OUTPUT_CONFIGURATION(gobject);
    GListModel *heads;
    HwdoutOutputHead *head;
    HwdoutOutputConfigurationHead *configuration_head;
    guint i;

    G_OBJECT_CLASS(hwdout_output_configuration_parent_class)->constructed(gobject);

    g_return_if_fail(HWDOUT_IS_OUTPUT_MANAGER(self->output_manager));
    self->serial = hwdout_output_manager_get_serial(self->output_manager);
    g_signal_connect_object(
        self->output_manager, "done", G_CALLBACK(handle_output_manager_done), self,
        G_CONNECT_DEFAULT
    );

    heads = hwdout_output_manager_get_heads(self->output_manager);
    for (i = 0; i < g_list_model_get_n_items(heads); i++) {
        head = HWDOUT_OUTPUT_HEAD(g_list_model_get_object(heads, i));
        g_return_if_fail(HWDOUT_IS_OUTPUT_HEAD(head));

        configuration_head = hwdout_output_configuration_head_new(self, head);
        g_list_store_append(self->heads, configuration_head);

        g_object_unref(G_OBJECT(head));
    }
}

static void
hwdout_output_configuration_dispose(GObject *gobject) {
    HwdoutOutputConfiguration *self = HWDOUT_OUTPUT_CONFIGURATION(gobject);

    g_clear_object(&self->output_manager);
    g_clear_object(&self->heads);

    G_OBJECT_CLASS(hwdout_output_configuration_parent_class)->dispose(gobject);
}

static void
hwdout_output_configuration_finalize(GObject *gobject) {
    HwdoutOutputConfiguration *self = HWDOUT_OUTPUT_CONFIGURATION(gobject);

    g_clear_pointer(&self->wlr_output_configuration, zwlr_output_configuration_v1_destroy);

    G_OBJECT_CLASS(hwdout_output_configuration_parent_class)->finalize(gobject);
}

static void
hwdout_output_configuration_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutOutputConfiguration *self = HWDOUT_OUTPUT_CONFIGURATION(gobject);

    switch ((HwdoutOutputConfigurationProperty)property_id) {
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
hwdout_output_configuration_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutOutputConfiguration *self = HWDOUT_OUTPUT_CONFIGURATION(gobject);

    switch ((HwdoutOutputConfigurationProperty)property_id) {
    case PROP_OUTPUT_MANAGER:
        g_value_set_object(value, hwdout_output_configuration_get_output_manager(self));
        break;

    case PROP_SERIAL:
        g_value_set_uint(value, hwdout_output_configuration_get_serial(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_configuration_class_init(HwdoutOutputConfigurationClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_output_configuration_constructed;
    object_class->dispose = hwdout_output_configuration_dispose;
    object_class->finalize = hwdout_output_configuration_finalize;
    object_class->set_property = hwdout_output_configuration_set_property;
    object_class->get_property = hwdout_output_configuration_get_property;

    /**
     * HwdoutOutputConfiguration:output-manager: (attributes org.gtk.Property.get=hwdout_output_configuration_get_output_manager)
     *
     * Reference to the output manager which this configuration applies to.
     */
    properties[PROP_OUTPUT_MANAGER] = g_param_spec_object(
        "output-manager", "Output Manager",
        "Reference to the output manager which this configuration applies to",
        HWDOUT_TYPE_OUTPUT_MANAGER, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    );

    /**
     * HwdoutOutputConfiguration:serial: (attributes org.gtk.Property.get=hwdout_output_configuration_get_serial)
     *
     * The serial number of the output manager when this configuration was
     * created.  The configuration will be invalidated (trigerring a `cancelled`
     * signal) if the manager's serial changes.
     */
    properties[PROP_SERIAL] = g_param_spec_uint(
        "serial", "Serial", "ID of the last done event, or zero",
        0,        // Min.
        UINT_MAX, // Max.
        0,        // Default.
        G_PARAM_READABLE
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    /**
     * HwdoutOutputConfiguration::succeeded:
     * @self: The `HwdoutOutputConfiguration`
     *
     * Signals that the configuration was successfully applied.
     */
    signals[SIGNAL_SUCCEEDED] = g_signal_new(
        g_intern_static_string("succeeded"), G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST,
        0,           // Closure.
        NULL,        // Accumulator.
        NULL,        // Accumulator data.
        NULL,        // C marshaller.
        G_TYPE_NONE, // Return type.
        0
    );

    /**
     * HwdoutOutputConfiguration::failed:
     * @self: The `HwdoutOutputConfiguration`
     *
     * Signals that the configuration was not acceptable and could not be
     * applied.
     */
    signals[SIGNAL_FAILED] = g_signal_new(
        g_intern_static_string("failed"), G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST,
        0,           // Closure.
        NULL,        // Accumulator.
        NULL,        // Accumulator data.
        NULL,        // C marshaller.
        G_TYPE_NONE, // Return type.
        0
    );

    /**
     * HwdoutOutputConfiguration::cancelled:
     * @self: The `HwdoutOutputConfiguration`
     *
     * Signals that the configuration was invalidated before it could be
     * applied.  If possible, the user should recreate the configuration based
     * on the new manager state and try again.
     */
    signals[SIGNAL_CANCELLED] = g_signal_new(
        g_intern_static_string("cancelled"), G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST,
        0,           // Closure.
        NULL,        // Accumulator.
        NULL,        // Accumulator data.
        NULL,        // C marshaller.
        G_TYPE_NONE, // Return type.
        0
    );
}

static void
hwdout_output_configuration_init(HwdoutOutputConfiguration *self) {
    self->heads = g_list_store_new(HWDOUT_TYPE_OUTPUT_CONFIGURATION_HEAD);
}

HwdoutOutputConfiguration *
hwdout_output_configuration_new(HwdoutOutputManager *output_manager) {
    return g_object_new(HWDOUT_TYPE_OUTPUT_CONFIGURATION, "output-manager", output_manager, NULL);
}

void
hwdout_output_configuration_apply(HwdoutOutputConfiguration *self) {
    g_return_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION(self));

    // TODO
}

/**
 * hwdout_output_configuration_get_output_manager: (attributes org.gtk.Method.get_property=output-manager)
 * @self: a `HwdoutOutputConfiguration`
 *
 * Gets the output manager that this object configures.
 *
 * Returns: (transfer none): The owning output manager.
 */
HwdoutOutputManager *
hwdout_output_configuration_get_output_manager(HwdoutOutputConfiguration *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION(self), NULL);

    return self->output_manager;
}

/**
 * hwdout_output_configuration_get_serial: (attributes org.gtk.Method.get_property=serial)
 * @self: a `HwdoutOutputConfiguration`
 *
 * The serial number of the output manager at the time this configuration was
 * created.
 */
guint
hwdout_output_configuration_get_serial(HwdoutOutputConfiguration *self) {
    g_return_val_if_fail(HWDOUT_IS_OUTPUT_CONFIGURATION(self), 0);

    return self->serial;
}
