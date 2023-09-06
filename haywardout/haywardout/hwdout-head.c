#include "hwdout-head.h"

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

#include "hwdout-manager.h"
#include "hwdout-mode.h"
#include "hwdout-transform.h"
#include "hwdout-util.h"

struct _HwdoutHeadState {
    gchar *name;
    gchar *description;

    // Width and height of the output in millimeters.
    gint physical_width;
    gint physical_height;

    GListStore *modes;

    gboolean is_enabled;

    HwdoutMode *current_mode;

    // Position of the top left corner in layout coordinates.
    gint x;
    gint y;

    HwdoutTransform transform;
    wl_fixed_t scale;

    gchar *make;
    gchar *model;
    gchar *serial_number;
};
typedef struct _HwdoutHeadState HwdoutHeadState;

struct _HwdoutHead {
    GObject parent_instance;

    GWeakRef manager;
    struct zwlr_output_head_v1 *wlr_output_head;

    HwdoutHeadState pending;
    HwdoutHeadState current;

    gboolean finished;
};

G_DEFINE_TYPE(HwdoutHead, hwdout_head, G_TYPE_OBJECT)

typedef enum {
    PROP_MANAGER = 1,
    PROP_WLR_HEAD,
    PROP_NAME,
    PROP_DESCRIPTION,
    PROP_PHYSICAL_WIDTH,
    PROP_PHYSICAL_HEIGHT,
    PROP_ENABLED,
    PROP_CURRENT_MODE,
    PROP_X,
    PROP_Y,
    PROP_TRANSFORM,
    PROP_SCALE,
    PROP_MAKE,
    PROP_MODEL,
    PROP_SERIAL_NUMBER,
    N_PROPERTIES,
} HwdoutHeadProperty;

static GParamSpec *properties[N_PROPERTIES];

typedef enum {
    SIGNAL_FINISHED = 1,
    N_SIGNALS,
} HwdoutHeadSignal;

static guint signals[N_SIGNALS] = {0};

static void
handle_head_name(void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *name) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: name=%s", (void *)wlr_output_head, name);

    g_clear_pointer(&self->pending.name, g_free);
    self->pending.name = g_strdup(name);
}

static void
handle_head_description(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *description
) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: description=%s", (void *)wlr_output_head, description);

    g_clear_pointer(&self->pending.description, g_free);
    self->pending.description = g_strdup(description);
}

static void
handle_head_physical_size(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t width, int32_t height
) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: physical size=(%imm x %imm)", (void *)wlr_output_head, width, height);

    self->pending.physical_width = width;
    self->pending.physical_height = height;
}

static void
handle_mode_finished(HwdoutMode *mode, uint32_t serial, void *data) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    guint position = 0;
    if (!g_list_store_find(self->pending.modes, mode, &position)) {
        g_warning("received mode finished event for unrecognised mode");
        return;
    }

    g_list_store_remove(self->pending.modes, position);
}

static void
handle_head_mode(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, struct zwlr_output_mode_v1 *wlr_mode
) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    HwdoutManager *manager;
    HwdoutMode *mode;

    g_debug("head=%p: mode: %p", (void *)wlr_output_head, (void *)wlr_mode);

    manager = HWDOUT_MANAGER(g_weak_ref_get(&self->manager));

    mode = hwdout_mode_new(manager, self, wlr_mode);
    g_return_if_fail(mode != NULL);
    g_list_store_append(self->pending.modes, mode);
    zwlr_output_mode_v1_set_user_data(wlr_mode, mode);

    g_signal_connect_object(
        mode, "finished", G_CALLBACK(handle_mode_finished), self, G_CONNECT_DEFAULT
    );
}

static void
handle_head_enabled(void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t enabled) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: enabled: %i", (void *)wlr_output_head, enabled);

    self->pending.is_enabled = TRUE;
}

static void
handle_head_current_mode(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, struct zwlr_output_mode_v1 *wlr_mode
) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    HwdoutMode *mode;

    g_debug("head=%p: mode: %p", (void *)wlr_output_head, (void *)wlr_mode);

    mode = zwlr_output_mode_v1_get_user_data(wlr_mode);
    self->pending.current_mode = HWDOUT_MODE(g_object_ref(mode));
}

static void
handle_head_position(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t x, int32_t y
) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: position: (%ix%i)", (void *)wlr_output_head, x, y);

    self->pending.x = x;
    self->pending.y = y;
}

static void
handle_head_transform(void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t transform) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: transform: %i", (void *)wlr_output_head, transform);

    self->pending.transform = transform;
}

static void
handle_head_scale(void *data, struct zwlr_output_head_v1 *wlr_output_head, wl_fixed_t scale) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: scale: %f", (void *)wlr_output_head, wl_fixed_to_double(scale));

    self->pending.scale = wl_fixed_to_double(scale);
}

/**
 * the head has disappeared
 *
 * This event indicates that the head is no longer available. The
 * head object becomes inert. Clients should send a destroy request
 * and release any resources associated with it.
 */
static void
handle_head_finished(void *data, struct zwlr_output_head_v1 *wlr_output_head) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    if (self->finished) {
        g_warning("received multiple finished events");
        return;
    }

    g_signal_emit(self, signals[SIGNAL_FINISHED], 0);
}

static void
handle_head_make(void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *make) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: make: %s", (void *)wlr_output_head, make);

    g_clear_pointer(&self->pending.make, g_free);
    self->pending.make = g_strdup(make);
}

static void
handle_head_model(void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *model) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: model: %s", (void *)wlr_output_head, model);

    g_clear_pointer(&self->pending.model, g_free);
    self->pending.model = g_strdup(model);
}

static void
handle_head_serial_number(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *serial_number
) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    g_debug("head=%p: serial number: %s", (void *)wlr_output_head, serial_number);

    g_clear_pointer(&self->pending.serial_number, g_free);
    self->pending.serial_number = g_strdup(serial_number);
}

/**
 * current adaptive sync state
 *
 * This event describes whether adaptive sync is currently
 * enabled for the head or not. Adaptive sync is also known as
 * Variable Refresh Rate or VRR.
 * @since 4
 */
static void
handle_head_adaptive_sync(void *data, struct zwlr_output_head_v1 *wlr_output_head, uint32_t state) {
    // TODO    HwdoutHead *self = HWDOUT_HEAD(data);
}

static const struct zwlr_output_head_v1_listener output_head_listener = {
    .name = handle_head_name,
    .description = handle_head_description,
    .physical_size = handle_head_physical_size,
    .mode = handle_head_mode,
    .enabled = handle_head_enabled,
    .current_mode = handle_head_current_mode,
    .position = handle_head_position,
    .transform = handle_head_transform,
    .scale = handle_head_scale,
    .finished = handle_head_finished,
    .make = handle_head_make,
    .model = handle_head_model,
    .serial_number = handle_head_serial_number,
    .adaptive_sync = handle_head_adaptive_sync,
};

static void
handle_manager_done(HwdoutManager *manager, uint32_t serial, void *data) {
    HwdoutHead *self = HWDOUT_HEAD(data);

    gboolean name_changed = false;
    gboolean description_changed = false;
    gboolean physical_width_changed = false;
    gboolean physical_height_changed = false;
    gboolean enabled_changed = false;
    gboolean current_mode_changed = false;
    gboolean x_changed = false;
    gboolean y_changed = false;
    gboolean transform_changed = false;
    gboolean scale_changed = false;
    gboolean make_changed = false;
    gboolean model_changed = false;
    gboolean serial_number_changed = false;

    if (g_strcmp0(self->current.name, self->pending.name) != 0) {
        g_free(self->current.name);
        self->current.name = g_strdup(self->pending.name);
        name_changed = true;
    }

    if (g_strcmp0(self->current.description, self->pending.description) != 0) {
        g_free(self->current.description);
        self->current.description = g_strdup(self->pending.description);
        description_changed = true;
    }

    if (self->current.physical_width != self->pending.physical_width) {
        self->current.physical_width = self->pending.physical_width;
        physical_width_changed = true;
    }

    if (self->current.physical_height != self->pending.physical_height) {
        self->pending.physical_height = self->pending.physical_height;
        physical_height_changed = true;
    }

    hwdout_copy_list_store(self->current.modes, self->pending.modes);

    if (self->current.is_enabled != self->pending.is_enabled) {
        self->current.is_enabled = self->pending.is_enabled;
        enabled_changed = true;
    }

    if (self->current.current_mode != self->pending.current_mode) {
        g_clear_object(&self->current.current_mode);
        self->current.current_mode = g_object_ref(self->pending.current_mode);
        current_mode_changed = true;
    }

    if (self->current.x != self->pending.x) {
        self->current.x = self->pending.x;
        x_changed = true;
    }

    if (self->current.y != self->pending.y) {
        self->current.y = self->pending.y;
        y_changed = true;
    }

    if (self->current.transform != self->pending.transform) {
        self->current.transform = self->pending.transform;
        transform_changed = true;
    }

    if (self->current.scale != self->pending.scale) {
        self->current.scale = self->pending.scale;
        scale_changed = true;
    }

    if (g_strcmp0(self->current.make, self->pending.make) != 0) {
        g_free(self->current.make);
        self->current.make = g_strdup(self->pending.make);
        make_changed = true;
    }

    if (g_strcmp0(self->current.model, self->pending.model) != 0) {
        g_free(self->current.model);
        self->current.model = g_strdup(self->pending.model);
        model_changed = true;
    }

    if (g_strcmp0(self->current.serial_number, self->pending.serial_number) != 0) {
        g_free(self->current.serial_number);
        self->current.serial_number = g_strdup(self->pending.serial_number);
        serial_number_changed = true;
    }

    if (name_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_NAME]);
    }
    if (description_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_DESCRIPTION]);
    }
    if (physical_width_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PHYSICAL_WIDTH]);
    }
    if (physical_height_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_PHYSICAL_HEIGHT]);
    }
    if (enabled_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ENABLED]);
    }
    if (current_mode_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_CURRENT_MODE]);
    }
    if (x_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_X]);
    }
    if (y_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_Y]);
    }
    if (transform_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_TRANSFORM]);
    }
    if (scale_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SCALE]);
    }
    if (make_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MAKE]);
    }
    if (model_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MODEL]);
    }
    if (serial_number_changed) {
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SERIAL_NUMBER]);
    }
}

static void
hwdout_head_constructed(GObject *gobject) {
    HwdoutHead *self = HWDOUT_HEAD(gobject);

    HwdoutManager *manager;

    G_OBJECT_CLASS(hwdout_head_parent_class)->constructed(gobject);

    manager = HWDOUT_MANAGER(g_weak_ref_get(&self->manager));
    g_return_if_fail(HWDOUT_IS_MANAGER(manager));
    g_signal_connect_object(
        manager, "done", G_CALLBACK(handle_manager_done), self, G_CONNECT_DEFAULT
    );

    g_return_if_fail(self->wlr_output_head != NULL);
    zwlr_output_head_v1_add_listener(self->wlr_output_head, &output_head_listener, self);
}

static void
hwdout_head_dispose(GObject *gobject) {
    HwdoutHead *self = HWDOUT_HEAD(gobject);

    g_weak_ref_clear(&self->manager);

    g_clear_pointer(&self->pending.name, g_free);
    g_clear_pointer(&self->current.name, g_free);

    g_clear_pointer(&self->pending.description, g_free);
    g_clear_pointer(&self->current.description, g_free);

    g_clear_object(&self->pending.modes);
    g_clear_object(&self->current.modes);

    g_clear_object(&self->pending.current_mode);
    g_clear_object(&self->current.current_mode);

    g_clear_pointer(&self->pending.make, g_free);
    g_clear_pointer(&self->current.make, g_free);

    g_clear_pointer(&self->pending.model, g_free);
    g_clear_pointer(&self->current.model, g_free);

    g_clear_pointer(&self->pending.serial_number, g_free);
    g_clear_pointer(&self->current.serial_number, g_free);

    G_OBJECT_CLASS(hwdout_head_parent_class)->dispose(gobject);
}

static void
hwdout_head_finalize(GObject *gobject) {
    HwdoutHead *self = HWDOUT_HEAD(gobject);

    g_clear_pointer(&self->wlr_output_head, zwlr_output_head_v1_destroy);

    G_OBJECT_CLASS(hwdout_head_parent_class)->finalize(gobject);
}

static void
hwdout_head_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutHead *self = HWDOUT_HEAD(gobject);

    switch ((HwdoutHeadProperty)property_id) {
    case PROP_MANAGER:
        g_weak_ref_set(&self->manager, g_value_get_object(value));
        break;

    case PROP_WLR_HEAD:
        self->wlr_output_head = g_value_get_pointer(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_head_get_property(GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec) {
    HwdoutHead *self = HWDOUT_HEAD(gobject);

    switch ((HwdoutHeadProperty)property_id) {
    case PROP_MANAGER:
        g_value_take_object(value, hwdout_head_get_manager(self));
        break;

    case PROP_WLR_HEAD:
        g_value_set_pointer(value, hwdout_head_get_wlr_output_head(self));
        break;

    case PROP_NAME:
        g_value_set_string(value, hwdout_head_get_name(self));
        break;

    case PROP_DESCRIPTION:
        g_value_set_string(value, hwdout_head_get_description(self));
        break;

    case PROP_PHYSICAL_WIDTH:
        g_value_set_int(value, hwdout_head_get_physical_width(self));
        break;

    case PROP_PHYSICAL_HEIGHT:
        g_value_set_int(value, hwdout_head_get_physical_height(self));
        break;

    case PROP_ENABLED:
        g_value_set_boolean(value, hwdout_head_get_is_enabled(self));
        break;

    case PROP_CURRENT_MODE:
        g_value_set_object(value, hwdout_head_get_current_mode(self));
        break;

    case PROP_X:
        g_value_set_int(value, hwdout_head_get_x(self));
        break;

    case PROP_Y:
        g_value_set_int(value, hwdout_head_get_y(self));
        break;

    case PROP_TRANSFORM:
        g_value_set_enum(value, hwdout_head_get_transform(self));
        break;

    case PROP_SCALE:
        g_value_set_double(value, hwdout_head_get_scale(self));
        break;

    case PROP_MAKE:
        g_value_set_string(value, hwdout_head_get_make(self));
        break;

    case PROP_MODEL:
        g_value_set_string(value, hwdout_head_get_model(self));
        break;

    case PROP_SERIAL_NUMBER:
        g_value_set_string(value, hwdout_head_get_serial_number(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_head_class_init(HwdoutHeadClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_head_constructed;
    object_class->dispose = hwdout_head_dispose;
    object_class->finalize = hwdout_head_finalize;
    object_class->set_property = hwdout_head_set_property;
    object_class->get_property = hwdout_head_get_property;

    properties[PROP_MANAGER] = g_param_spec_object(
        "manager", "Output manager", "Output manager that owns this head", HWDOUT_TYPE_MANAGER,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_WLR_HEAD] = g_param_spec_pointer(
        "wlr-output-head", "WLR output head",
        "WLRoots output head reference that this object wraps",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_NAME] = g_param_spec_string(
        "name", "Name", "Short unique name that identifies the the output",
        NULL, // Default.
        G_PARAM_READABLE
    );

    properties[PROP_DESCRIPTION] = g_param_spec_string(
        "description", "Description", "Human readable description of the output",
        NULL, // Default.
        G_PARAM_READABLE
    );

    properties[PROP_PHYSICAL_WIDTH] = g_param_spec_int(
        "physical-width", "Physical width", "Width of the display in millimetres",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );
    properties[PROP_PHYSICAL_HEIGHT] = g_param_spec_int(
        "physical-height", "Physical height", "Height of the display in millimetres",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );

    properties[PROP_ENABLED] = g_param_spec_boolean(
        "is-enabled", "Is enabled", "Whether or not the compositor is rendering to this display",
        FALSE, // Default.
        G_PARAM_READABLE
    );

    properties[PROP_CURRENT_MODE] = g_param_spec_object(
        "current-mode", "Current mode",
        "Reference to the mode object representing the currently active mode",
        hwdout_mode_get_type(), G_PARAM_READABLE
    );

    properties[PROP_X] = g_param_spec_int(
        "x", "X", "Offset of leftmost edge of the output in layout coordinates",
        INT_MIN, // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );
    properties[PROP_Y] = g_param_spec_int(
        "y", "Y", "Offset of topmost edge of the output in layout coordinates",
        INT_MIN, // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );

    properties[PROP_TRANSFORM] = g_param_spec_enum(
        "transform", "Transform", "Describes how the display is currently rotated and or flipped",
        HWDOUT_TYPE_TRANSFORM, HWDOUT_TRANSFORM_NORMAL, G_PARAM_READABLE
    );

    properties[PROP_SCALE] = g_param_spec_double(
        "scale", "Scale",
        "Amount by which layout coordinates should be multiplied to map to physical coordinates",
        0.000001, // Minimum.
        100.0,    // Maximum,
        1.0,      // Default.
        G_PARAM_READABLE
    );

    properties[PROP_MAKE] = g_param_spec_string(
        "make", "Make", "The manufacturer of the display",
        NULL, // Default.
        G_PARAM_READABLE
    );

    properties[PROP_MODEL] = g_param_spec_string(
        "model", "Model", "The name of the model of the display",
        NULL, // Default.
        G_PARAM_READABLE
    );

    properties[PROP_SERIAL_NUMBER] = g_param_spec_string(
        "serial-number", "Serial number", "The unique serial number of the display",
        NULL, // Default.
        G_PARAM_READABLE
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    signals[SIGNAL_FINISHED] = g_signal_new(
        g_intern_static_string("finished"), G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST,
        0,           // Closure.
        NULL,        // Accumulator.
        NULL,        // Accumulator data.
        NULL,        // C marshaller.
        G_TYPE_NONE, // Return type.
        0
    );
}

static void
hwdout_head_init(HwdoutHead *self) {
    self->pending.modes = g_list_store_new(HWDOUT_TYPE_MODE);
    self->current.modes = g_list_store_new(HWDOUT_TYPE_MODE);

    self->pending.scale = 1.0;
    self->current.scale = 1.0;
}

HwdoutHead *
hwdout_head_new(HwdoutManager *manager, struct zwlr_output_head_v1 *wlr_output_head) {
    return g_object_new(
        HWDOUT_TYPE_HEAD, "manager", manager, "wlr-output-head", wlr_output_head, NULL
    );
}

/**
 * hwdout_head_get_manager: (attributes org.gtk.Method.get_property=output-manager)
 * @self: a `HwdoutHead`
 *
 * Returns: (transfer full): The owning output manager.
 */
HwdoutManager *
hwdout_head_get_manager(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return g_weak_ref_get(&self->manager);
}

struct zwlr_output_head_v1 *
hwdout_head_get_wlr_output_head(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->wlr_output_head;
}

gchar *
hwdout_head_get_name(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->current.name;
}

gchar *
hwdout_head_get_description(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->current.description;
}

gint
hwdout_head_get_physical_width(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), 0);

    return self->current.physical_width;
}

gint
hwdout_head_get_physical_height(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), 0);

    return self->current.physical_height;
}

gboolean
hwdout_head_get_is_enabled(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), FALSE);

    return self->current.is_enabled;
}

/**
 * hwdout_head_get_modes:
 *
 * Returns: (transfer none) a `GListModel` of `HwdoutMode`s.
 */
GListModel *
hwdout_head_get_modes(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return G_LIST_MODEL(self->current.modes);
}

/**
 * hwdout_head_get_current_mode:
 *
 * Returns: (transfer none) the current mode.
 */
HwdoutMode *
hwdout_head_get_current_mode(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->current.current_mode;
}

gint
hwdout_head_get_x(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), 0);

    return self->current.x;
}

gint
hwdout_head_get_y(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), 0);

    return self->current.y;
}

HwdoutTransform
hwdout_head_get_transform(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), HWDOUT_TRANSFORM_NORMAL);

    return self->current.transform;
}

double
hwdout_head_get_scale(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), 1.0);

    return self->current.scale;
}

/**
 * hwdout_head_get_make:
 *
 * Returns: (transfer none): The name of the manufacturer.
 */
gchar *
hwdout_head_get_make(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->current.make;
}

/**
 * hwdout_head_get_model:
 *
 * Returns: (transfer none): The name given by the manufacturer to the type of output.
 */
gchar *
hwdout_head_get_model(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->current.model;
}

/**
 * hwdout_head_get_serial_number:
 *
 * Returns: (transfer none): The identifier given by the manufacturer to this specific output.
 */
gchar *
hwdout_head_get_serial_number(HwdoutHead *self) {
    g_return_val_if_fail(HWDOUT_IS_HEAD(self), NULL);

    return self->current.serial_number;
}
