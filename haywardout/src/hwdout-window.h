#ifndef __HWDOUT_WINDOW_H__
#define __HWDOUT_WINDOW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define HWDOUT_TYPE_WINDOW (hwdout_window_get_type())
G_DECLARE_FINAL_TYPE(HwdoutWindow, hwdout_window, HWDOUT, WINDOW, GtkWindow)

HwdoutWindow *
hwdout_window_new();

G_END_DECLS

#endif /* __HWDOUT_WINDOW_H__ */
