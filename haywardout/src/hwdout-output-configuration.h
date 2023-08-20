#ifndef __HWDOUT_OUTPUT_CONFIGURATION_H__
#define __HWDOUT_OUTPUT_CONFIGURATION_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _HwdoutOutputManager HwdoutOutputManager;

#define HWDOUT_TYPE_OUTPUT_CONFIGURATION (hwdout_output_configuration_get_type())
G_DECLARE_FINAL_TYPE(
    HwdoutOutputConfiguration, hwdout_output_configuration, HWDOUT, OUTPUT_CONFIGURATION, GObject
)

HwdoutOutputConfiguration *
hwdout_output_configuration_new(HwdoutOutputManager *manager);

void
hwdout_output_configuration_apply(HwdoutOutputConfiguration *configuration);

HwdoutOutputManager *
hwdout_output_configuration_get_output_manager(HwdoutOutputConfiguration *self);

guint
hwdout_output_configuration_get_serial(HwdoutOutputConfiguration *self);

G_END_DECLS

#endif /* __HWDOUT_OUTPUT_CONFIGURATION_H__ */
