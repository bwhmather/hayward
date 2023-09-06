#ifndef __HWDOUT_MODE_H__
#define __HWDOUT_MODE_H__

#include <glib-object.h>
#include <stdint.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

G_BEGIN_DECLS

typedef struct _HwdoutManager HwdoutManager;
typedef struct _HwdoutHead HwdoutHead;

#define HWDOUT_TYPE_MODE (hwdout_mode_get_type())
G_DECLARE_FINAL_TYPE(HwdoutMode, hwdout_mode, HWDOUT, MODE, GObject)

HwdoutMode *
hwdout_mode_new(
    HwdoutManager *manager, HwdoutHead *head, struct zwlr_output_mode_v1 *wlr_output_head
);

gint
hwdout_mode_get_width(HwdoutMode *self);

gint
hwdout_mode_get_height(HwdoutMode *self);

gint
hwdout_mode_get_refresh(HwdoutMode *self);

gboolean
hwdout_mode_get_is_preferred(HwdoutMode *self);

G_END_DECLS

#endif /* __HWDOUT_MODE_H__ */
