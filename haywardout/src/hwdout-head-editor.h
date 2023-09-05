#ifndef __HWDOUT_HEAD_EDITOR_H__
#define __HWDOUT_HEAD_EDITOR_H__

#include "hwdout-configuration-head.h"

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define HWDOUT_TYPE_HEAD_EDITOR (hwdout_head_editor_get_type())
G_DECLARE_FINAL_TYPE(HwdoutHeadEditor, hwdout_head_editor, HWDOUT, HEAD_EDITOR, GtkWidget)

HwdoutHeadEditor *
hwdout_head_editor_new(void);

void
hwdout_head_editor_set_head(HwdoutHeadEditor *editor, HwdoutConfigurationHead *head);

HwdoutConfigurationHead *
hwdout_head_editor_get_head(HwdoutHeadEditor *editor);

G_END_DECLS

#endif /* __HWDOUT_HEAD_EDITOR_H__ */
