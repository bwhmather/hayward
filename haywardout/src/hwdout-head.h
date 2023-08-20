#ifndef __HWDOUT_HEAD_H__
#define __HWDOUT_HEAD_H__

#include "hwdout-mode.h"
#include "hwdout-transform.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <stdint.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

G_BEGIN_DECLS

typedef struct _HwdoutManager HwdoutManager;

#define HWDOUT_TYPE_HEAD (hwdout_head_get_type())
G_DECLARE_FINAL_TYPE(HwdoutHead, hwdout_head, HWDOUT, HEAD, GObject)

HwdoutHead *
hwdout_head_new(HwdoutManager *manager, struct zwlr_output_head_v1 *wlr_output_head);

HwdoutManager *
hwdout_head_get_manager(HwdoutHead *self);

struct zwlr_output_head_v1 *
hwdout_head_get_wlr_output_head(HwdoutHead *self);

gchar *
hwdout_head_get_name(HwdoutHead *self);

gchar *
hwdout_head_get_description(HwdoutHead *self);

gint
hwdout_head_get_physical_width(HwdoutHead *self);

gint
hwdout_head_get_physical_height(HwdoutHead *self);

gboolean
hwdout_head_get_is_enabled(HwdoutHead *self);

GListModel *
hwdout_head_get_modes(HwdoutHead *self);

HwdoutMode *
hwdout_head_get_current_mode(HwdoutHead *self);

gint
hwdout_head_get_x(HwdoutHead *self);

gint
hwdout_head_get_y(HwdoutHead *self);

HwdoutTransform
hwdout_head_get_transform(HwdoutHead *self);

double
hwdout_head_get_scale(HwdoutHead *self);

gchar *
hwdout_head_get_make(HwdoutHead *self);

gchar *
hwdout_head_get_model(HwdoutHead *self);

gchar *
hwdout_head_get_serial_number(HwdoutHead *self);

G_END_DECLS

#endif /* __HWDOUT_HEAD_H__ */
