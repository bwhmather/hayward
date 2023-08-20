#ifndef __HWDOUT_OUTPUT_MODE_H__
#define __HWDOUT_OUTPUT_MODE_H__

#include <glib-object.h>
#include <stdint.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

G_BEGIN_DECLS

typedef struct _HwdoutOutputManager HwdoutOutputManager;
typedef struct _HwdoutOutputHead HwdoutOutputHead;

#define HWDOUT_TYPE_OUTPUT_MODE (hwdout_output_mode_get_type())
G_DECLARE_FINAL_TYPE(HwdoutOutputMode, hwdout_output_mode, HWDOUT, OUTPUT_MODE, GObject)

HwdoutOutputMode *
hwdout_output_mode_new(
    HwdoutOutputManager *manager, HwdoutOutputHead *head,
    struct zwlr_output_mode_v1 *wlr_output_head
);

gint
hwdout_output_mode_get_width(HwdoutOutputMode *self);

gint
hwdout_output_mode_get_height(HwdoutOutputMode *self);

gint
hwdout_output_mode_get_refresh(HwdoutOutputMode *self);

gboolean
hwdout_output_mode_get_is_preferred(HwdoutOutputMode *self);

G_END_DECLS

#endif /* __HWDOUT_OUTPUT_MODE_H__ */
