#ifndef __HWDOUT_CONFIGURATION_H__
#define __HWDOUT_CONFIGURATION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _HwdoutManager HwdoutManager;

#define HWDOUT_TYPE_CONFIGURATION (hwdout_configuration_get_type())
G_DECLARE_FINAL_TYPE(HwdoutConfiguration, hwdout_configuration, HWDOUT, CONFIGURATION, GObject)

HwdoutConfiguration *
hwdout_configuration_new(HwdoutManager *manager);

void
hwdout_configuration_apply(HwdoutConfiguration *configuration);

HwdoutManager *
hwdout_configuration_get_manager(HwdoutConfiguration *self);

guint
hwdout_configuration_get_serial(HwdoutConfiguration *self);

G_END_DECLS

#endif /* __HWDOUT_CONFIGURATION_H__ */
