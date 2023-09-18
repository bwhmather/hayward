#include "hwdout-configuration.h"

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

#include "hwdout-configuration-head.h"
#include "hwdout-head.h"
#include "hwdout-manager.h"
#include "hwdout-util.h"

struct _HwdoutConfiguration {
    GObject parent_instance;

    struct zwlr_output_configuration_v1 *wlr_output_configuration;

    HwdoutManager *manager;
    uint32_t serial;

    GListStore *heads; // `HwdoutConfigurationHead`.

    gboolean is_dirty;
    gboolean finished;
};

G_DEFINE_TYPE(HwdoutConfiguration, hwdout_configuration, G_TYPE_OBJECT)

typedef enum {
    PROP_MANAGER = 1,
    PROP_SERIAL,
    PROP_HEADS,
    PROP_IS_DIRTY,
    N_PROPERTIES
} HwdoutConfigurationProperty;

static GParamSpec *properties[N_PROPERTIES];

typedef enum {
    SIGNAL_SUCCEEDED = 1,
    SIGNAL_FAILED,
    SIGNAL_CANCELLED,
    N_SIGNALS,
} HwdoutConfigurationSignal;

static guint signals[N_SIGNALS] = {0};

static gboolean
hwdout_configuration_check_is_dirty(HwdoutConfiguration *self) {
    HwdoutConfigurationHead *config_head;
    guint i;

    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION(self), FALSE);

    for (i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(self->heads)); i++) {
        config_head =
            HWDOUT_CONFIGURATION_HEAD(g_list_model_get_item(G_LIST_MODEL(self->heads), i));
        if (hwdout_configuration_head_get_is_dirty(config_head)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void
hwdout_configuration_update_is_dirty(HwdoutConfiguration *self) {
    gboolean is_dirty;

    g_return_if_fail(HWDOUT_IS_CONFIGURATION(self));

    is_dirty = hwdout_configuration_check_is_dirty(self);
    if (self->is_dirty == is_dirty) {
        return;
    }

    self->is_dirty = is_dirty;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_IS_DIRTY]);
}

static void
handle_configuration_head_is_dirty_changed(
    HwdoutConfigurationHead *config_head, GParamSpec *pspec, gpointer user_data
) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(user_data);

    g_return_if_fail(HWDOUT_IS_CONFIGURATION(self));
    g_return_if_fail(HWDOUT_IS_CONFIGURATION_HEAD(config_head));

    if (hwdout_configuration_head_get_is_dirty(config_head) == self->is_dirty) {
        return;
    }

    hwdout_configuration_update_is_dirty(self);
}

static void
handle_wlr_output_manager_done(HwdoutManager *manager, uint32_t serial, void *data) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(data);

    if (self->finished) {
        return;
    }

    self->finished = true;
    g_clear_pointer(&self->wlr_output_configuration, zwlr_output_configuration_v1_destroy);
    g_signal_emit(self, signals[SIGNAL_CANCELLED], 0);
}

static void
hwdout_configuration_constructed(GObject *gobject) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(gobject);
    GListModel *heads;
    HwdoutHead *head;
    HwdoutConfigurationHead *configuration_head;
    guint i;

    G_OBJECT_CLASS(hwdout_configuration_parent_class)->constructed(gobject);

    g_return_if_fail(HWDOUT_IS_MANAGER(self->manager));
    self->serial = hwdout_manager_get_serial(self->manager);
    g_signal_connect_object(
        self->manager, "done", G_CALLBACK(handle_wlr_output_manager_done), self, G_CONNECT_DEFAULT
    );

    heads = hwdout_manager_get_heads(self->manager);
    for (i = 0; i < g_list_model_get_n_items(heads); i++) {
        head = HWDOUT_HEAD(g_list_model_get_object(heads, i));
        g_return_if_fail(HWDOUT_IS_HEAD(head));

        configuration_head = hwdout_configuration_head_new(self, head);
        g_list_store_append(self->heads, configuration_head);

        g_signal_connect_object(
            configuration_head, "notify::is-dirty",
            G_CALLBACK(handle_configuration_head_is_dirty_changed), self, G_CONNECT_DEFAULT
        );

        g_object_unref(G_OBJECT(head));
    }
}

static void
hwdout_configuration_dispose(GObject *gobject) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(gobject);

    g_clear_object(&self->manager);
    g_clear_object(&self->heads);

    G_OBJECT_CLASS(hwdout_configuration_parent_class)->dispose(gobject);
}

static void
hwdout_configuration_finalize(GObject *gobject) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(gobject);

    g_clear_pointer(&self->wlr_output_configuration, zwlr_output_configuration_v1_destroy);

    G_OBJECT_CLASS(hwdout_configuration_parent_class)->finalize(gobject);
}

static void
hwdout_configuration_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(gobject);

    switch ((HwdoutConfigurationProperty)property_id) {
    case PROP_MANAGER:
        g_clear_object(&self->manager);
        self->manager = g_value_dup_object(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_configuration_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutConfiguration *self = HWDOUT_CONFIGURATION(gobject);

    switch ((HwdoutConfigurationProperty)property_id) {
    case PROP_MANAGER:
        g_value_set_object(value, hwdout_configuration_get_manager(self));
        break;

    case PROP_SERIAL:
        g_value_set_uint(value, hwdout_configuration_get_serial(self));
        break;

    case PROP_HEADS:
        g_value_set_object(value, hwdout_configuration_get_heads(self));
        break;

    case PROP_IS_DIRTY:
        g_value_set_double(value, hwdout_configuration_get_is_dirty(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_configuration_class_init(HwdoutConfigurationClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_configuration_constructed;
    object_class->dispose = hwdout_configuration_dispose;
    object_class->finalize = hwdout_configuration_finalize;
    object_class->set_property = hwdout_configuration_set_property;
    object_class->get_property = hwdout_configuration_get_property;

    /**
     * HwdoutConfiguration:manager: (attributes org.gtk.Property.get=hwdout_configuration_get_manager)
     *
     * Reference to the output manager which this configuration applies to.
     */
    properties[PROP_MANAGER] = g_param_spec_object(
        "manager", "Output Manager",
        "Reference to the output manager which this configuration applies to", HWDOUT_TYPE_MANAGER,
        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    );

    /**
     * HwdoutConfiguration:serial: (attributes org.gtk.Property.get=hwdout_configuration_get_serial)
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

    /**
     * HwdoutConfiguration:heads: (attributes org.gtk.Property.get=hwdout_configuration_get_heads)
     *
     * A `GListModel` containing all of `HwdoutConfigurationHead` corresponding
     * to heads owned by the manager.
     */
    properties[PROP_HEADS] = g_param_spec_object(
        "heads", "Output heads", "List model containing all of the heads managed by this instance",
        G_TYPE_LIST_MODEL, G_PARAM_READABLE
    );

    /**
     * HwdoutConfiguration:is-dirty: (attributes org.gtk.Property.get=hwdout_configuration_get_is_dirty)
     *
     * A boolean indicating whether this configuration contains any changes from
     * the current state.
     */
    properties[PROP_IS_DIRTY] = g_param_spec_boolean(
        "is-dirty", "Dirty", "Whether this object contains any changes from the current state",
        FALSE, // Default.
        G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    /**
     * HwdoutConfiguration::succeeded:
     * @self: The `HwdoutConfiguration`
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
     * HwdoutConfiguration::failed:
     * @self: The `HwdoutConfiguration`
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
     * HwdoutConfiguration::cancelled:
     * @self: The `HwdoutConfiguration`
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
hwdout_configuration_init(HwdoutConfiguration *self) {
    self->heads = g_list_store_new(HWDOUT_TYPE_CONFIGURATION_HEAD);
}

HwdoutConfiguration *
hwdout_configuration_new(HwdoutManager *manager) {
    return g_object_new(HWDOUT_TYPE_CONFIGURATION, "manager", manager, NULL);
}

void
hwdout_configuration_apply(HwdoutConfiguration *self) {
    HwdoutConfigurationHead *config_head;
    HwdoutHead *head;
    HwdoutMode *mode;
    struct zwlr_output_manager_v1 *wlr_manager;
    struct zwlr_output_configuration_v1 *wlr_config;
    struct zwlr_output_head_v1 *wlr_head;
    struct zwlr_output_configuration_head_v1 *wlr_config_head;
    struct zwlr_output_mode_v1 *wlr_mode;
    guint i;

    g_return_if_fail(HWDOUT_IS_CONFIGURATION(self));

    if (!hwdout_configuration_get_is_dirty(self)) {
        return;
    }

    wlr_manager = hwdout_manager_get_wlr_output_manager(self->manager);

    wlr_config = zwlr_output_manager_v1_create_configuration(wlr_manager, self->serial);

    for (i = 0; i < g_list_model_get_n_items(G_LIST_MODEL(self->heads)); i++) {
        config_head =
            HWDOUT_CONFIGURATION_HEAD(g_list_model_get_object(G_LIST_MODEL(self->heads), i));
        head = hwdout_configuration_head_get_head(config_head);
        wlr_head = hwdout_head_get_wlr_output_head(head);

        if (!hwdout_configuration_head_get_is_enabled(config_head)) {
            g_warning("Disabling head");
            zwlr_output_configuration_v1_disable_head(wlr_config, wlr_head);
            continue;
        }

        wlr_config_head = zwlr_output_configuration_v1_enable_head(wlr_config, wlr_head);

        mode = hwdout_configuration_head_get_mode(config_head);
        if (mode != NULL) {
            wlr_mode = hwdout_mode_get_wlr_mode(mode);
            zwlr_output_configuration_head_v1_set_mode(wlr_config_head, wlr_mode);
        } else {
            // TODO custom modes.
            g_warning("Custom modes not implemented");
        }

        zwlr_output_configuration_head_v1_set_position(
            wlr_config_head, hwdout_configuration_head_get_x(config_head),
            hwdout_configuration_head_get_y(config_head)
        );

        g_clear_object(&head);
    }

    zwlr_output_configuration_v1_apply(wlr_config);
}

/**
 * hwdout_configuration_get_manager: (attributes org.gtk.Method.get_property=manager)
 * @self: a `HwdoutConfiguration`
 *
 * Gets the output manager that this object configures.
 *
 * Returns: (transfer none): The owning output manager.
 */
HwdoutManager *
hwdout_configuration_get_manager(HwdoutConfiguration *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION(self), NULL);

    return self->manager;
}

/**
 * hwdout_configuration_get_serial: (attributes org.gtk.Method.get_property=serial)
 * @self: a `HwdoutConfiguration`
 *
 * The serial number of the output manager at the time this configuration was
 * created.
 */
guint
hwdout_configuration_get_serial(HwdoutConfiguration *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION(self), 0);

    return self->serial;
}

/**
 * hwdout_configuration_get_heads: (attributes org.gtk.Method.get_property=heads)
 * @self: a `HwdoutConfiguration`
 *
 * Returns: (transfer none): A list model containing all `HwdoutConfigurationHeads` owned by this object.
 */
GListModel *
hwdout_configuration_get_heads(HwdoutConfiguration *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION(self), NULL);

    return G_LIST_MODEL(self->heads);
}

/**
 * hwdout_configuration_get_is_dirty: (attributes org.gtk.Method.get_property=is-dirty)
 * @self: a `HwdoutConfiguration`
 *
 * Returns: TRUE if this configuration deviates from the current configuration, FALSE otherwise.
 */
gboolean
hwdout_configuration_get_is_dirty(HwdoutConfiguration *self) {
    g_return_val_if_fail(HWDOUT_IS_CONFIGURATION(self), FALSE);

    return self->is_dirty;
}
