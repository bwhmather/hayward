#include "hwdout-manager.h"

#include "hwdout-head.h"
#include "hwdout-util.h"

#include <gio/gio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

struct _HwdoutManagerState {
    GListStore *heads;
};
typedef struct _HwdoutManagerState HwdoutManagerState;

struct _HwdoutManager {
    GObject parent_instance;

    struct zwlr_output_manager_v1 *wlr_output_manager;

    uint32_t serial;

    HwdoutManagerState pending;
    HwdoutManagerState current;

    gboolean finished;
};

G_DEFINE_TYPE(HwdoutManager, hwdout_manager, G_TYPE_OBJECT)

typedef enum { PROP_WLR_MANAGER = 1, PROP_SERIAL, PROP_HEADS, N_PROPERTIES } HwdoutManagerProperty;

static GParamSpec *properties[N_PROPERTIES];

typedef enum {
    SIGNAL_DONE = 1,
    SIGNAL_FINISHED,
    N_SIGNALS,
} HwdoutManagerSignal;

static guint signals[N_SIGNALS] = {0};

static void
handle_head_finished(HwdoutHead *head, uint32_t serial, void *data) {
    HwdoutManager *self = HWDOUT_MANAGER(data);

    guint position = 0;
    if (!g_list_store_find(self->pending.heads, head, &position)) {
        g_warning("received head finished event for unrecognised head");
        return;
    }

    g_list_store_remove(self->pending.heads, position);
}

void
handle_manager_head(
    void *data, struct zwlr_output_manager_v1 *zwlr_output_manager_v1,
    struct zwlr_output_head_v1 *wlr_head
) {
    HwdoutManager *self = HWDOUT_MANAGER(data);
    HwdoutHead *head;

    if (self->finished) {
        g_warning("received head event after finished");
        return;
    }

    head = hwdout_head_new(self, wlr_head);
    g_list_store_append(self->pending.heads, head);
    g_object_unref(head);

    g_signal_connect_object(
        head, "finished", G_CALLBACK(handle_head_finished), self, G_CONNECT_DEFAULT
    );
}

void
handle_manager_done(
    void *data, struct zwlr_output_manager_v1 *zwlr_output_manager_v1, uint32_t serial
) {
    HwdoutManager *self = HWDOUT_MANAGER(data);

    self->serial = serial;

    hwdout_copy_list_store(self->current.heads, self->pending.heads);

    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SERIAL]);

    g_signal_emit(self, signals[SIGNAL_DONE], 0, serial);
}

void
handle_manager_finished(void *data, struct zwlr_output_manager_v1 *zwlr_output_manager_v1) {
    HwdoutManager *self = HWDOUT_MANAGER(data);

    if (self->finished) {
        g_warning("received multiple finished events");
        return;
    }

    g_signal_emit(self, signals[SIGNAL_FINISHED], 0);
}

static const struct zwlr_output_manager_v1_listener manager_listener = {
    .head = handle_manager_head,
    .done = handle_manager_done,
    .finished = handle_manager_finished,
};

static void
hwdout_manager_constructed(GObject *gobject) {
    HwdoutManager *self = HWDOUT_MANAGER(gobject);

    G_OBJECT_CLASS(hwdout_manager_parent_class)->constructed(gobject);

    zwlr_output_manager_v1_add_listener(self->wlr_output_manager, &manager_listener, self);
}

static void
hwdout_manager_dispose(GObject *gobject) {
    HwdoutManager *self = HWDOUT_MANAGER(gobject);

    g_clear_object(&self->pending.heads);
    g_clear_object(&self->current.heads);

    G_OBJECT_CLASS(hwdout_manager_parent_class)->dispose(gobject);
}

static void
hwdout_manager_finalize(GObject *gobject) {
    HwdoutManager *self = HWDOUT_MANAGER(gobject);

    g_clear_pointer(&self->wlr_output_manager, zwlr_output_manager_v1_destroy);

    G_OBJECT_CLASS(hwdout_manager_parent_class)->finalize(gobject);
}

static void
hwdout_manager_set_property(
    GObject *gobject, guint property_id, const GValue *value, GParamSpec *pspec
) {
    HwdoutManager *self = HWDOUT_MANAGER(gobject);

    switch ((HwdoutManagerProperty)property_id) {
    case PROP_WLR_MANAGER:
        self->wlr_output_manager = g_value_get_pointer(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_manager_get_property(GObject *gobject, guint property_id, GValue *value, GParamSpec *pspec) {
    HwdoutManager *self = HWDOUT_MANAGER(gobject);

    g_return_if_fail(HWDOUT_IS_MANAGER(self));

    switch ((HwdoutManagerProperty)property_id) {
    case PROP_WLR_MANAGER:
        g_value_set_pointer(value, hwdout_manager_get_wlr_output_manager(self));
        break;

    case PROP_SERIAL:
        g_value_set_uint(value, hwdout_manager_get_serial(self));
        break;

    case PROP_HEADS:
        g_value_set_object(value, hwdout_manager_get_heads(self));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, property_id, pspec);
        break;
    }
}

static void
hwdout_manager_class_init(HwdoutManagerClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->constructed = hwdout_manager_constructed;
    object_class->dispose = hwdout_manager_dispose;
    object_class->finalize = hwdout_manager_finalize;
    object_class->set_property = hwdout_manager_set_property;
    object_class->get_property = hwdout_manager_get_property;

    /**
     * HwdoutManager:wlr-output-manager: (attributes org.gtk.Property.get=hwdout_manager_get_wlr_output_manager)
     *
     * A pointer to the `struct zwlr_output_manager_v1` that this object wraps.
     *
     * The `HwdoutManager` takes full ownership of this object and is
     * responible for destroying it once it is finished.
     */
    properties[PROP_WLR_MANAGER] = g_param_spec_pointer(
        "wlr-output-manager", "WLR output manager",
        "WLRoots output manager reference that this object wraps",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE
    );

    /**
     * HwdoutManager:serial: (attributes org.gtk.Property.get=hwdout_manager_get_serial)
     *
     * The serial number of the last `done` event, or 0 if none received yet.
     */
    properties[PROP_SERIAL] = g_param_spec_uint(
        "serial", "Serial", "ID of the last done event, or zero",
        0,        // Min.
        UINT_MAX, // Max.
        0,        // Default.
        G_PARAM_READABLE
    );

    /**
     * HwdoutManager:heads: (attributes org.gtk.Property.get=hwdout_manager_get_heads)
     *
     * A `GListModel` containing the currently available `HwdoutHead`
     * objects owned by the manager.
     */
    properties[PROP_HEADS] = g_param_spec_object(
        "heads", "Output heads", "List model containing all of the heads managed by this instance",
        G_TYPE_LIST_MODEL, G_PARAM_READABLE
    );

    g_object_class_install_properties(object_class, N_PROPERTIES, properties);

    /**
     * HwdoutManager::done:
     * @self: The `HwdoutManager`
     * @serial: The new serial number received with the `done` event.
     *
     * Signals that the compositor is done sending changes and that the client
     * should apply them atomically.
     *
     * Consumers should register with %G_SIGNAL_RUN_LAST in order to update
     * after all `HwdoutHead` and `HwdoutMode` objects owned by
     * this manager have been updated.
     */
    signals[SIGNAL_DONE] = g_signal_new(
        g_intern_static_string("done"), G_TYPE_FROM_CLASS(object_class), G_SIGNAL_RUN_LAST,
        0,           // Closure.
        NULL,        // Accumulator.
        NULL,        // Accumulator data.
        NULL,        // C marshaller.
        G_TYPE_NONE, // Return type.
        1, G_TYPE_UINT
    );

    /**
     * HwdoutManager::finished:
     * @self: The `HwdoutManager`
     *
     * Signals that the compositor is deleting the output manager and will not
     * be sending any further events.
     *
     * This object will no longer be useable after this signal is raised.  Users
     * should drop their references and either exit or recreate.
     */
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
hwdout_manager_init(HwdoutManager *self) {
    self->pending.heads = g_list_store_new(HWDOUT_TYPE_HEAD);
    self->current.heads = g_list_store_new(HWDOUT_TYPE_HEAD);
}

HwdoutManager *
hwdout_manager_new(struct zwlr_output_manager_v1 *wlr_output_manager) {
    return g_object_new(HWDOUT_TYPE_MANAGER, "wlr-output-manager", wlr_output_manager, NULL);
}

/**
 * hwdout_manager_get_wlr_output_manager: (attributes org.gtk.Method.get_property=wlr-output-manager)
 * @self: a `HwdoutManager`
 *
 * Returns: a pointer to the `struct zwlr_output_manager_v1` that the `HwdoutManager` wraps.
 */
struct zwlr_output_manager_v1 *
hwdout_manager_get_wlr_output_manager(HwdoutManager *self) {
    g_return_val_if_fail(HWDOUT_IS_MANAGER(self), NULL);

    return self->wlr_output_manager;
}

/**
 * hwdout_manager_get_serial: (attributes org.gtk.Method.get_property=serial)
 * @self: a `HwdoutManager`
 *
 * Returns: The serial number of the last `done` event, or 0.
 */
guint
hwdout_manager_get_serial(HwdoutManager *self) {
    g_return_val_if_fail(HWDOUT_IS_MANAGER(self), 0);

    return self->serial;
}

/**
 * hwdout_manager_get_heads: (attributes org.gtk.Method.get_property=heads)
 * @self: a `HwdoutManager`
 *
 * Returns: (transfer none): A `GListModel` of `HwdoutHeads`.
 */
GListModel *
hwdout_manager_get_heads(HwdoutManager *self) {
    g_return_val_if_fail(HWDOUT_IS_MANAGER(self), NULL);

    return G_LIST_MODEL(self->current.heads);
}
