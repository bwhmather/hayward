#include "hwdout-application.h"

int
main(int argc, char *argv[]) {
    HwdoutApplication *app;
    int status;

    app = hwdout_application_new("com.bwhmather.haywardout");
    status = hwdout_application_run(app, argc, argv);
    g_object_unref(app);

    exit(status);
}
