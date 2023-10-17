#ifndef __HWDOUT_HEAD_EDITOR_H__
#define __HWDOUT_HEAD_EDITOR_H__

#include <gtk/gtk.h>

#include "hwdout-configuration-head.h"

G_BEGIN_DECLS

#define HWDOUT_TYPE_HEAD_EDITOR (hwdout_head_editor_get_type())
G_DECLARE_FINAL_TYPE(HwdoutHeadEditor, hwdout_head_editor, HWDOUT, HEAD_EDITOR, GtkWidget)

HwdoutHeadEditor *
hwdout_head_editor_new(void);

void
hwdout_head_editor_set_head(HwdoutHeadEditor *editor, HwdoutConfigurationHead *head);
HwdoutConfigurationHead *
hwdout_head_editor_get_head(HwdoutHeadEditor *editor);

void
hwdout_head_editor_set_head_name(HwdoutHeadEditor *self, const gchar *name);
const gchar *
hwdout_head_editor_get_head_name(HwdoutHeadEditor *self);

void
hwdout_head_editor_set_head_description(HwdoutHeadEditor *self, const gchar *description);
const gchar *
hwdout_head_editor_get_head_description(HwdoutHeadEditor *self);

void
hwdout_head_editor_set_head_is_enabled(HwdoutHeadEditor *self, gboolean is_enabled);
gboolean
hwdout_head_editor_get_head_is_enabled(HwdoutHeadEditor *self);

void
hwdout_head_editor_set_head_mode(HwdoutHeadEditor *self, HwdoutMode *mode);
HwdoutMode *
hwdout_head_editor_get_head_mode(HwdoutHeadEditor *self);

void
hwdout_head_editor_set_head_modes(HwdoutHeadEditor *self, GListModel *modes);
GListModel *
hwdout_head_editor_get_head_modes(HwdoutHeadEditor *self);

void
hwdout_head_editor_set_head_transform(HwdoutHeadEditor *self, HwdoutTransform transform);
HwdoutTransform
hwdout_head_editor_get_head_transform(HwdoutHeadEditor *self);

void
hwdout_head_editor_set_head_scale(HwdoutHeadEditor *self, double scale);
double
hwdout_head_editor_get_head_scale(HwdoutHeadEditor *self);
G_END_DECLS

#endif /* __HWDOUT_HEAD_EDITOR_H__ */
