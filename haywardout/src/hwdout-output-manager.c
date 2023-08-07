#include "hwdout-output-manager.h"

#include "hwdout-output-head.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _HwdoutOutputManager {
    GObject parent_instance;

    struct zwlr_output_manager_v1 *wlr_output_manager;

    uint32_t serial;
};

G_DEFINE_TYPE(HwdoutOutputManager, hwdout_output_manager, G_TYPE_OBJECT)

typedef enum { PROP_WLR_OUTPUT_MANAGER = 1, PROP_SERIAL, N_PROPERTIES } HwdoutOutputManagerProperty;

static GParamSpec *properties[N_PROPERTIES];

typedef enum {
    SIGNAL_DONE = 1,
    N_SIGNALS,
} HwdoutOutputHeadSignal;

static guint signals[N_SIGNALS] = {0};

void
handle_manager_head(
    void *data, struct zwlr_output_manager_v1 *zwlr_output_manager_v1,
    struct zwlr_output_head_v1 *head
) {
    HwdoutOutputManager *self = HWDOUT_OUTPUT_MANAGER(data);

    hwdout_output_head_new(self, head);
}

void
handle_manager_done(
    void *data, struct zwlr_output_manager_v1 *zwlr_output_manager_v1, uint32_t serial
) {
    HwdoutOutputManager *self = HWDOUT_OUTPUT_MANAGER(data);

    self->serial = serial;

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SERIAL]);

    g_signal_emit(self, signals[SIGNAL_DONE], 0, serial);
}

void
handle_manager_finished(void *data, struct zwlr_output_manager_v1 *zwlr_output_manager_v1) {}

static const struct zwlr_output_manager_v1_listener manager_listener = {
    .head = handle_manager_head,
    .done = handle_manager_done,
    .finished = handle_manager_finished,
};

static void
hwdout_output_manager_constructed(GObject *gobject) {
    HwdoutOutputManager *self = HWDOUT_OUTPUT_MANAGER(gobject);

    G_OBJECT_CLASS(hwdout_output_manager_parent_class)->constructed(gobject);

    zwlr_output_manager_v1_add_listener(self->wlr_output_manager, &manager_listener, self);
}

static void
hwdout_output_manager_dispose(GObject *gobject) {
    G_OBJECT_CLASS(hwdout_output_manager_parent_class)->dispose(gobject);
}

static void
hwdout_output_manager_finalize(GObject *gobject) {
    G_OBJECT_CLASS(hwdout_output_manager_parent_class)->finalize(gobject);
}

static void
hwdout_output_manager_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutOutputManager *self = HWDOUT_OUTPUT_MANAGER(gobject);

    switch ((HwdoutOutputManagerProperty)property_id) {
    case PROP_WLR_OUTPUT_MANAGER:
        self->wlr_output_manager = g_value_get_pointer(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_manager_get_property(
    GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec
) {
    HwdoutOutputManager *self = HWDOUT_OUTPUT_MANAGER(gobject);

    switch ((HwdoutOutputManagerProperty)property_id) {
    case PROP_WLR_OUTPUT_MANAGER:
        g_value_set_pointer(value, self->wlr_output_manager);
        break;

    case PROP_SERIAL:
        g_value_set_uint(value, self->serial);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_output_manager_class_init(HwdoutOutputManagerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_output_manager_constructed;
    object_class->dispose = hwdout_output_manager_dispose;
    object_class->finalize = hwdout_output_manager_finalize;
    object_class->set_property = hwdout_output_manager_set_property;
    object_class->get_property = hwdout_output_manager_get_property;

    properties[PROP_WLR_OUTPUT_MANAGER] = g_param_spec_pointer(
        "wlr-output-manager", "WLR output manager",
        "WLRoots output manager reference that this object wraps",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    properties[PROP_SERIAL] = g_param_spec_uint(
        "serial", "Serial", "ID of the last done event, or zero",
        0,        // Min.
        UINT_MAX, // Max.
        0,        // Default.
        G_PARAM_READABLE
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    signals[SIGNAL_DONE] = g_signal_new(
        g_intern_static_string("done"), G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST,
        0,           // Closure.
        NULL,        // Accumulator.
        NULL,        // Accumulator data.
        NULL,        // C marshaller.
        G_TYPE_NONE, // Return type.
        1, G_TYPE_UINT
    );
}

static void
hwdout_output_manager_init(HwdoutOutputManager *self) {}

HwdoutOutputManager *
hwdout_output_manager_new(struct zwlr_output_manager_v1 *wlr_output_manager) {
    return g_object_new(HWDOUT_TYPE_OUTPUT_MANAGER, "wlr_output_manager", wlr_output_manager, NULL);
}
