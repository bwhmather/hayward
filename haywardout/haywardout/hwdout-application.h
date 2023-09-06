#ifndef __HWDOUT_APPLICATION_H__
#define __HWDOUT_APPLICATION_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define HWDOUT_TYPE_APPLICATION (hwdout_application_get_type())
G_DECLARE_FINAL_TYPE(HwdoutApplication, hwdout_application, HWDOUT, APPLICATION, GObject)

HwdoutApplication *
hwdout_application_new(const gchar *application_id);

int
hwdout_application_run(HwdoutApplication *self, int argc, char *argv[]);

G_END_DECLS

#endif /* __HWDOUT_APPLICATION_H__ */
