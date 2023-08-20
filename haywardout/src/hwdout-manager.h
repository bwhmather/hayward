#ifndef __HWDOUT_MANAGER_H__
#define __HWDOUT_MANAGER_H__

#include <gio/gio.h>
#include <glib-object.h>
#include <stdint.h>
#include <wayland-client.h>

#include <wlr-output-management-unstable-v1-client-protocol.h>

G_BEGIN_DECLS

#define HWDOUT_TYPE_MANAGER (hwdout_manager_get_type())
G_DECLARE_FINAL_TYPE(HwdoutManager, hwdout_manager, HWDOUT, MANAGER, GObject)

HwdoutManager *
hwdout_manager_new(struct zwlr_output_manager_v1 *wlr_output_manager);

struct zwlr_output_manager_v1 *
hwdout_manager_get_wlr_output_manager(HwdoutManager *self);

guint
hwdout_manager_get_serial(HwdoutManager *self);

GListModel *
hwdout_manager_get_heads(HwdoutManager *self);

G_END_DECLS

#endif /* __HWDOUT_MANAGER_H__ */
