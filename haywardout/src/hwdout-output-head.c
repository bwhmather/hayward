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

struct _HwdoutOutputHeadState {
    char *name;
    char *description;

    // Width and height of the output in millimeters.
    int32_t physical_width;
    int32_t physical_height;

    GListStore *modes;

    bool enabled;

    HwdoutOutputMode *current_mode;

    // Position of the top left corner in layout coordinates.
    int32_t x;
    int32_t y;

    HwdoutOutputTransform transform;
    wl_fixed_t scale;

    char *make;
    char *model;
    char *serial_number;
};
typedef struct _HwdoutOutputHeadState HwdoutOutputHeadState;

struct _HwdoutOutputHead {
    GObject parent_instance;

    GWeakRef manager;
    struct zwlr_output_head_v1 *wlr_output_head;

    HwdoutOutputHeadState pending;
    HwdoutOutputHeadState current;
};

G_DEFINE_TYPE(HwdoutOutputHead, hwdout_output_head, G_TYPE_OBJECT)

typedef enum {
    PROP_MANAGER = 1,
    PROP_WLR_OUTPUT_HEAD,
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
} HwdoutOutputHeadProperty;

static GParamSpec *properties[N_PROPERTIES];

static void
handle_head_name(void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *name) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: name=%s", (void *)wlr_output_head, name);

    g_clear_pointer(&self->pending.name, g_free);
    self->pending.name = g_strdup(name);
}

static void
handle_head_description(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *description
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: description=%s", (void *)wlr_output_head, description);

    g_clear_pointer(&self->pending.description, g_free);
    self->pending.description = g_strdup(description);
}

static void
handle_head_physical_size(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t width, int32_t height
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: physical size=(%imm x %imm)", (void *)wlr_output_head, width, height);

    self->pending.physical_width = width;
    self->pending.physical_height = height;
}

static void
handle_head_mode(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, struct zwlr_output_mode_v1 *wlr_mode
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    HwdoutOutputManager *manager;
    HwdoutOutputMode *mode;

    g_debug("head=%p: mode: %p", (void *)wlr_output_head, (void *)wlr_mode);

    manager = HWDOUT_OUTPUT_MANAGER(g_weak_ref_get(&self->manager));

    mode = hwdout_output_mode_new(manager, self, wlr_mode);
    g_return_if_fail(mode != NULL);
    g_list_store_append(self->pending.modes, mode);
    zwlr_output_mode_v1_set_user_data(wlr_mode, mode);
}

static void
handle_head_enabled(void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t enabled) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: enabled: %i", (void *)wlr_output_head, enabled);

    self->pending.enabled = true;
}

static void
handle_head_current_mode(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, struct zwlr_output_mode_v1 *wlr_mode
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    HwdoutOutputMode *mode;

    g_debug("head=%p: mode: %p", (void *)wlr_output_head, (void *)wlr_mode);

    mode = zwlr_output_mode_v1_get_user_data(wlr_mode);
    self->pending.current_mode = HWDOUT_OUTPUT_MODE(g_object_ref(mode));
}

static void
handle_head_position(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t x, int32_t y
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: position: (%ix%i)", (void *)wlr_output_head, x, y);

    self->pending.x = x;
    self->pending.y = y;
}

static void
handle_head_transform(void *data, struct zwlr_output_head_v1 *wlr_output_head, int32_t transform) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: transform: %i", (void *)wlr_output_head, transform);

    self->pending.transform = transform;
}

static void
handle_head_scale(void *data, struct zwlr_output_head_v1 *wlr_output_head, wl_fixed_t scale) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

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
    // HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);
    // TODO
}

static void
handle_head_make(void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *make) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: make: %s", (void *)wlr_output_head, make);

    g_clear_pointer(&self->pending.make, g_free);
    self->pending.make = g_strdup(make);
}

static void
handle_head_model(void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *model) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

    g_debug("head=%p: model: %s", (void *)wlr_output_head, model);

    g_clear_pointer(&self->pending.model, g_free);
    self->pending.model = g_strdup(model);
}

static void
handle_head_serial_number(
    void *data, struct zwlr_output_head_v1 *wlr_output_head, const char *serial_number
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

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
    // TODO    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);
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
handle_manager_done(HwdoutOutputManager *manager, uint32_t serial, void *data) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(data);

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

    if (self->current.enabled != self->pending.enabled) {
        self->current.enabled = self->pending.enabled;
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
hwdout_output_head_constructed(GObject *gobject) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(gobject);

    HwdoutOutputManager *manager;

    G_OBJECT_CLASS(hwdout_output_head_parent_class)->constructed(gobject);

    manager = HWDOUT_OUTPUT_MANAGER(g_weak_ref_get(&self->manager));
    g_return_if_fail(HWDOUT_IS_OUTPUT_MANAGER(manager));
    g_signal_connect_object(
        manager, "done", G_CALLBACK(handle_manager_done), self, G_CONNECT_DEFAULT
    );

    g_return_if_fail(self->wlr_output_head != NULL);
    zwlr_output_head_v1_add_listener(self->wlr_output_head, &output_head_listener, self);
}

static void
hwdout_output_head_dispose(GObject *gobject) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(gobject);

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

    G_OBJECT_CLASS(hwdout_output_head_parent_class)->dispose(gobject);
}

static void
hwdout_output_head_finalize(GObject *gobject) {
    G_OBJECT_CLASS(hwdout_output_head_parent_class)->finalize(gobject);
}

static void
hwdout_output_head_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(gobject);

    switch ((HwdoutOutputHeadProperty)property_id) {
    case PROP_MANAGER:
        g_weak_ref_set(&self->manager, g_value_get_object(value));
        break;

    case PROP_WLR_OUTPUT_HEAD:
        self->wlr_output_head = g_value_get_pointer(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_head_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutOutputHead *self = HWDOUT_OUTPUT_HEAD(gobject);

    switch ((HwdoutOutputHeadProperty)property_id) {
    case PROP_MANAGER:
        g_value_set_object(value, g_weak_ref_get(&self->manager));
        break;

    case PROP_WLR_OUTPUT_HEAD:
        g_value_set_pointer(value, self->wlr_output_head);
        break;

    case PROP_NAME:
        g_value_set_string(value, self->current.name);
        break;

    case PROP_DESCRIPTION:
        g_value_set_string(value, self->current.description);
        break;

    case PROP_PHYSICAL_WIDTH:
        g_value_set_int(value, self->current.physical_width);
        break;

    case PROP_PHYSICAL_HEIGHT:
        g_value_set_int(value, self->current.physical_height);
        break;

    case PROP_ENABLED:
        g_value_set_boolean(value, self->current.enabled);
        break;

    case PROP_CURRENT_MODE:
        g_value_set_object(value, self->current.current_mode);
        break;

    case PROP_X:
        g_value_set_int(value, self->current.x);
        break;

    case PROP_Y:
        g_value_set_int(value, self->current.y);
        break;

    case PROP_TRANSFORM:
        g_value_set_enum(value, self->current.transform);
        break;

    case PROP_SCALE:
        g_value_set_double(value, self->current.scale);
        break;

    case PROP_MAKE:
        g_value_set_string(value, self->current.make);
        break;

    case PROP_MODEL:
        g_value_set_string(value, self->current.model);
        break;

    case PROP_SERIAL_NUMBER:
        g_value_set_string(value, self->current.serial_number);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_head_class_init(HwdoutOutputHeadClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_output_head_constructed;
    object_class->dispose = hwdout_output_head_dispose;
    object_class->finalize = hwdout_output_head_finalize;
    object_class->set_property = hwdout_output_head_set_property;
    object_class->get_property = hwdout_output_head_get_property;

    properties[PROP_MANAGER] = g_param_spec_object(
        "manager", "Manager", "Output manager that owns this head", HWDOUT_TYPE_OUTPUT_MANAGER,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_WLR_OUTPUT_HEAD] = g_param_spec_pointer(
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
        "enabled", "Enabled", "Whether or not the compositor is rendering to this display",
        FALSE, // Default.
        G_PARAM_READABLE
    );

    properties[PROP_CURRENT_MODE] = g_param_spec_object(
        "current-mode", "Current mode",
        "Reference to the mode object representing the currently active mode",
        hwdout_output_mode_get_type(), G_PARAM_READABLE
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
        HWDOUT_TYPE_OUTPUT_TRANSFORM, HWDOUT_OUTPUT_TRANSFORM_NORMAL, G_PARAM_READABLE
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
}

static void
hwdout_output_head_init(HwdoutOutputHead *self) {
    self->pending.modes = g_list_store_new(HWDOUT_TYPE_OUTPUT_MODE);
    self->current.modes = g_list_store_new(HWDOUT_TYPE_OUTPUT_MODE);

    self->pending.scale = 1.0;
    self->current.scale = 1.0;
}

HwdoutOutputHead *
hwdout_output_head_new(HwdoutOutputManager *manager, struct zwlr_output_head_v1 *wlr_output_head) {
    return g_object_new(
        HWDOUT_TYPE_OUTPUT_HEAD, "manager", manager, "wlr-output-head", wlr_output_head, NULL
    );
}
