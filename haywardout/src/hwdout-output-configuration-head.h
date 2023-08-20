#ifndef __HWDOUT_OUTPUT_CONFIGURATION_HEAD_H__
#define __HWDOUT_OUTPUT_CONFIGURATION_HEAD_H__

#include "hwdout-output-transform.h"

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _HwdoutOutputConfiguration HwdoutOutputConfiguration;
typedef struct _HwdoutOutputManager HwdoutOutputManager;
typedef struct _HwdoutOutputHead HwdoutOutputHead;
typedef struct _HwdoutOutputMode HwdoutOutputMode;

#define HWDOUT_TYPE_OUTPUT_CONFIGURATION_HEAD (hwdout_output_configuration_head_get_type())
G_DECLARE_FINAL_TYPE(
    HwdoutOutputConfigurationHead, hwdout_output_configuration_head, HWDOUT,
    OUTPUT_CONFIGURATION_HEAD, GObject
)

HwdoutOutputConfigurationHead *
hwdout_output_configuration_head_new(
    HwdoutOutputConfiguration *configuration, HwdoutOutputHead *head
);

HwdoutOutputConfiguration *
hwdout_output_configuration_head_get_output_configuration(HwdoutOutputConfigurationHead *self);

HwdoutOutputManager *
hwdout_output_configuration_head_get_output_manager(HwdoutOutputConfigurationHead *self);
HwdoutOutputHead *
hwdout_output_configuration_head_get_output_head(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_mode(
    HwdoutOutputConfigurationHead *self, HwdoutOutputMode *mode
);
HwdoutOutputMode *
hwdout_output_configuration_head_get_mode(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_width(HwdoutOutputConfigurationHead *self, gint width);
gint
hwdout_output_configuration_head_get_width(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_height(HwdoutOutputConfigurationHead *self, gint height);
gint
hwdout_output_configuration_head_get_height(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_refresh(HwdoutOutputConfigurationHead *self, gint refresh);
gint
hwdout_output_configuration_head_get_refresh(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_x(HwdoutOutputConfigurationHead *self, gint x);
gint
hwdout_output_configuration_head_get_x(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_y(HwdoutOutputConfigurationHead *self, gint y);
gint
hwdout_output_configuration_head_get_y(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_transform(
    HwdoutOutputConfigurationHead *self, HwdoutOutputTransform transform
);
HwdoutOutputTransform
hwdout_output_configuration_head_get_transform(HwdoutOutputConfigurationHead *self);

void
hwdout_output_configuration_head_set_scale(HwdoutOutputConfigurationHead *self, double scale);
double
hwdout_output_configuration_head_get_scale(HwdoutOutputConfigurationHead *self);

G_END_DECLS

#endif /* __HWDOUT_OUTPUT_CONFIGURATION_HEAD_H__ */
