#ifndef __HWDOUT_OUTPUT_HEAD_H__
#define __HWDOUT_OUTPUT_HEAD_H__

#include "hwdout-output-mode.h"
#include "hwdout-output-transform.h"

#include <gio/gio.h>
#include <glib-object.h>
#include <stdint.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

G_BEGIN_DECLS

typedef struct _HwdoutOutputManager HwdoutOutputManager;

#define HWDOUT_TYPE_OUTPUT_HEAD (hwdout_output_head_get_type())
G_DECLARE_FINAL_TYPE(HwdoutOutputHead, hwdout_output_head, HWDOUT, OUTPUT_HEAD, GObject)

HwdoutOutputHead *
hwdout_output_head_new(HwdoutOutputManager *manager, struct zwlr_output_head_v1 *wlr_output_head);

HwdoutOutputManager *
hwdout_output_head_get_output_manager(HwdoutOutputHead *self);

struct zwlr_output_head_v1 *
hwdout_output_head_get_wlr_output_head(HwdoutOutputHead *self);

gchar *
hwdout_output_head_get_name(HwdoutOutputHead *self);

gchar *
hwdout_output_head_get_description(HwdoutOutputHead *self);

gint
hwdout_output_head_get_physical_width(HwdoutOutputHead *self);

gint
hwdout_output_head_get_physical_height(HwdoutOutputHead *self);

gboolean
hwdout_output_head_get_is_enabled(HwdoutOutputHead *self);

GListModel *
hwdout_output_head_get_modes(HwdoutOutputHead *self);

HwdoutOutputMode *
hwdout_output_head_get_current_mode(HwdoutOutputHead *self);

gint
hwdout_output_head_get_x(HwdoutOutputHead *self);

gint
hwdout_output_head_get_y(HwdoutOutputHead *self);

HwdoutOutputTransform
hwdout_output_head_get_transform(HwdoutOutputHead *self);

double
hwdout_output_head_get_scale(HwdoutOutputHead *self);

gchar *
hwdout_output_head_get_make(HwdoutOutputHead *self);

gchar *
hwdout_output_head_get_model(HwdoutOutputHead *self);

gchar *
hwdout_output_head_get_serial_number(HwdoutOutputHead *self);

G_END_DECLS

#endif /* __HWDOUT_OUTPUT_HEAD_H__ */
