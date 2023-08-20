#include "hwdout-mode.h"

#include "hwdout-head.h"
#include "hwdout-manager.h"
#include "hwdout-transform.h"

#include <gdk/gdk.h>
#include <gdk/wayland/gdkwayland.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _HwdoutModeState {
    // Size in physical pixels.
    int32_t width;
    int32_t height;

    // Vertical refresh rate in mHz or zero.
    int32_t refresh;

    bool preferred;
};

typedef struct _HwdoutModeState HwdoutModeState;

struct _HwdoutMode {
    GObject parent_instance;

    GWeakRef manager;
    GWeakRef head;
    struct zwlr_output_mode_v1 *wlr_output_mode;

    HwdoutModeState pending;
    HwdoutModeState current;

    gboolean finished;
};

G_DEFINE_TYPE(HwdoutMode, hwdout_mode, G_TYPE_OBJECT)

typedef enum {
    PROP_MANAGER = 1,
    PROP_HEAD,
    PROP_WLR_MODE,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_REFRESH,
    PROP_PREFERRED,
    N_PROPERTIES,
} HwdoutModeProperty;

static GParamSpec *properties[N_PROPERTIES];

typedef enum {
    SIGNAL_FINISHED = 1,
    N_SIGNALS,
} HwdoutModeSignal;

static guint signals[N_SIGNALS] = {0};

static void
handle_mode_size(
    void *data, struct zwlr_output_mode_v1 *wlr_output_mode, int32_t width, int32_t height
) {
    g_debug("mode=%p: size=(%ix%i)", (void *)wlr_output_mode, width, height);
}

static void
handle_mode_refresh(void *data, struct zwlr_output_mode_v1 *wlr_output_mode, int32_t refresh) {
    g_debug("mode=%p: refresh=%imHz", (void *)wlr_output_mode, refresh);
}

static void
handle_mode_preferred(void *data, struct zwlr_output_mode_v1 *wlr_output_mode) {
    g_debug("mode=%p: preferred", (void *)wlr_output_mode);
}

static void
handle_mode_finished(void *data, struct zwlr_output_mode_v1 *wlr_output_mode) {
    // TODO
}

static const struct zwlr_output_mode_v1_listener output_mode_listener = {
    .size = handle_mode_size,
    .refresh = handle_mode_refresh,
    .preferred = handle_mode_preferred,
    .finished = handle_mode_finished,
};

static void
hwdout_mode_constructed(GObject *gobject) {
    HwdoutMode *self = HWDOUT_MODE(gobject);

    G_OBJECT_CLASS(hwdout_mode_parent_class)->constructed(gobject);

    g_return_if_fail(self->wlr_output_mode != NULL);

    zwlr_output_mode_v1_add_listener(self->wlr_output_mode, &output_mode_listener, self);
}

static void
hwdout_mode_dispose(GObject *gobject) {
    G_OBJECT_CLASS(hwdout_mode_parent_class)->dispose(gobject);
}

static void
hwdout_mode_finalize(GObject *gobject) {
    HwdoutMode *self = HWDOUT_MODE(gobject);

    g_clear_pointer(&self->wlr_output_mode, zwlr_output_mode_v1_destroy);

    G_OBJECT_CLASS(hwdout_mode_parent_class)->finalize(gobject);
}

static void
hwdout_mode_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutMode *self = HWDOUT_MODE(gobject);

    switch ((HwdoutModeProperty)property_id) {
    case PROP_MANAGER:
        g_weak_ref_set(&self->manager, g_value_get_object(value));
        break;

    case PROP_HEAD:
        g_weak_ref_set(&self->head, g_value_get_object(value));
        break;

    case PROP_WLR_MODE:
        self->wlr_output_mode = g_value_get_pointer(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_mode_get_property(GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec) {
    HwdoutMode *self = HWDOUT_MODE(gobject);

    switch ((HwdoutModeProperty)property_id) {
    case PROP_MANAGER:
        g_value_set_object(value, g_weak_ref_get(&self->manager));
        break;

    case PROP_HEAD:
        g_value_set_object(value, g_weak_ref_get(&self->head));
        break;

    case PROP_WLR_MODE:
        g_value_set_pointer(value, self->wlr_output_mode);
        break;

    case PROP_WIDTH:
        g_value_set_int(value, self->current.width);
        break;

    case PROP_HEIGHT:
        g_value_set_int(value, self->current.height);
        break;

    case PROP_REFRESH:
        g_value_set_int(value, self->current.refresh);
        break;

    case PROP_PREFERRED:
        g_value_set_boolean(value, self->current.preferred);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_mode_class_init(HwdoutModeClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_mode_constructed;
    object_class->dispose = hwdout_mode_dispose;
    object_class->finalize = hwdout_mode_finalize;
    object_class->set_property = hwdout_mode_set_property;
    object_class->get_property = hwdout_mode_get_property;

    properties[PROP_MANAGER] = g_param_spec_object(
        "manager", "Manager", "Output manager that owns this mode", HWDOUT_TYPE_MANAGER,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_HEAD] = g_param_spec_object(
        "head", "Head", "The display that supports this mode", HWDOUT_TYPE_HEAD,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_WLR_MODE] = g_param_spec_pointer(
        "wlr-output-mode", "WLR output mode",
        "WLRoots output mode reference that this object wraps",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_WIDTH] = g_param_spec_int(
        "width", "Width", "Horizontal size of the output buffer in pixels",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );
    properties[PROP_HEIGHT] = g_param_spec_int(
        "height", "Height", "Vertical size of the output buffer in pixels",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );

    properties[PROP_REFRESH] = g_param_spec_int(
        "refresh", "Refresh rate", "Refresh rate in mHz or zero.",
        0,       // Minimum.
        INT_MAX, // Maximum.
        0,       // Default.
        G_PARAM_READABLE
    );

    properties[PROP_PREFERRED] = g_param_spec_boolean(
        "is-preferred", "Is preferred", "Set on the native mode for the display",
        FALSE, // Default.
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
hwdout_mode_init(HwdoutMode *self) {}

HwdoutMode *
hwdout_mode_new(
    HwdoutManager *manager, HwdoutHead *head, struct zwlr_output_mode_v1 *wlr_output_mode
) {
    return g_object_new(
        HWDOUT_TYPE_MODE, "manager", manager, "head", head, "wlr-output-mode", wlr_output_mode, NULL
    );
}

gint
hwdout_mode_get_width(HwdoutMode *self) {
    g_return_val_if_fail(HWDOUT_IS_MODE(self), 0);

    return self->current.width;
}

gint
hwdout_mode_get_height(HwdoutMode *self) {
    g_return_val_if_fail(HWDOUT_IS_MODE(self), 0);

    return self->current.height;
}

gint
hwdout_mode_get_refresh(HwdoutMode *self) {
    g_return_val_if_fail(HWDOUT_IS_MODE(self), 0);

    return self->current.refresh;
}

gboolean
hwdout_mode_get_is_preferred(HwdoutMode *self) {
    g_return_val_if_fail(HWDOUT_IS_MODE(self), 0);

    return self->current.preferred ? TRUE : FALSE;
}