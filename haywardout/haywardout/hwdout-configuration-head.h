#ifndef __HWDOUT_CONFIGURATION_HEAD_H__
#define __HWDOUT_CONFIGURATION_HEAD_H__

#include <glib-object.h>

#include "hwdout-transform.h"

G_BEGIN_DECLS

typedef struct _HwdoutConfiguration HwdoutConfiguration;
typedef struct _HwdoutManager HwdoutManager;
typedef struct _HwdoutHead HwdoutHead;
typedef struct _HwdoutMode HwdoutMode;

#define HWDOUT_TYPE_CONFIGURATION_HEAD (hwdout_configuration_head_get_type())
G_DECLARE_FINAL_TYPE(
    HwdoutConfigurationHead, hwdout_configuration_head, HWDOUT, CONFIGURATION_HEAD, GObject
)

HwdoutConfigurationHead *
hwdout_configuration_head_new(HwdoutConfiguration *configuration, HwdoutHead *head);

HwdoutConfiguration *
hwdout_configuration_head_get_configuration(HwdoutConfigurationHead *self);

HwdoutManager *
hwdout_configuration_head_get_manager(HwdoutConfigurationHead *self);
HwdoutHead *
hwdout_configuration_head_get_head(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_is_enabled(HwdoutConfigurationHead *self, gboolean enabled);
gboolean
hwdout_configuration_head_get_is_enabled(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_mode(HwdoutConfigurationHead *self, HwdoutMode *mode);
HwdoutMode *
hwdout_configuration_head_get_mode(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_width(HwdoutConfigurationHead *self, gint width);
gint
hwdout_configuration_head_get_width(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_height(HwdoutConfigurationHead *self, gint height);
gint
hwdout_configuration_head_get_height(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_refresh(HwdoutConfigurationHead *self, gint refresh);
gint
hwdout_configuration_head_get_refresh(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_x(HwdoutConfigurationHead *self, gint x);
gint
hwdout_configuration_head_get_x(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_y(HwdoutConfigurationHead *self, gint y);
gint
hwdout_configuration_head_get_y(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_transform(HwdoutConfigurationHead *self, HwdoutTransform transform);
HwdoutTransform
hwdout_configuration_head_get_transform(HwdoutConfigurationHead *self);

void
hwdout_configuration_head_set_scale(HwdoutConfigurationHead *self, double scale);
double
hwdout_configuration_head_get_scale(HwdoutConfigurationHead *self);

G_END_DECLS

#endif /* __HWDOUT_CONFIGURATION_HEAD_H__ */
