#ifndef __HWDOUT_OUTPUT_HEAD_H__
#define __HWDOUT_OUTPUT_HEAD_H__

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

G_END_DECLS

#endif /* __HWDOUT_OUTPUT_HEAD_H__ */
