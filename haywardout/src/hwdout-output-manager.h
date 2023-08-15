#ifndef __HWDOUT_OUTPUT_MANAGER_H__
#define __HWDOUT_OUTPUT_MANAGER_H__

#include <glib-object.h>
#include <stdint.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

G_BEGIN_DECLS

#define HWDOUT_TYPE_OUTPUT_MANAGER (hwdout_output_manager_get_type())
G_DECLARE_FINAL_TYPE(HwdoutOutputManager, hwdout_output_manager, HWDOUT, OUTPUT_MANAGER, GObject)

HwdoutOutputManager *
hwdout_output_manager_new(struct zwlr_output_manager_v1 *wlr_output_manager);

struct zwlr_output_manager_v1 *
hwdout_output_manager_get_wlr_output_manager(HwdoutOutputManager *self);

guint
hwdout_output_manager_get_serial(HwdoutOutputManager *self);

G_END_DECLS

#endif /* __HWDOUT_OUTPUT_MANAGER_H__ */
