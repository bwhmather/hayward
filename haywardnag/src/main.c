#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdlib.h>

#include <hayward-common/list.h>
#include <hayward-common/log.h>

#include <haywardnag/config.h>
#include <haywardnag/haywardnag.h>
#include <haywardnag/types.h>

static struct haywardnag haywardnag;

void
sig_handler(int signal) {
    haywardnag_destroy(&haywardnag);
    exit(EXIT_FAILURE);
}

void
hwd_terminate(int code) {
    haywardnag_destroy(&haywardnag);
    exit(code);
}

int
main(int argc, char **argv) {
    int status = EXIT_SUCCESS;

    list_t *types = create_list();
    haywardnag_types_add_default(types);

    haywardnag.buttons = create_list();
    wl_list_init(&haywardnag.outputs);
    wl_list_init(&haywardnag.seats);

    char *config_path = NULL;
    bool debug = false;
    status = haywardnag_parse_options(argc, argv, NULL, NULL, NULL, &config_path, &debug);
    if (status != 0) {
        goto cleanup;
    }
    hwd_log_init(debug ? HWD_DEBUG : HWD_ERROR);

    if (!config_path) {
        config_path = haywardnag_get_config_path();
    }
    if (config_path) {
        hwd_log(HWD_DEBUG, "Loading config file: %s", config_path);
        status = haywardnag_load_config(config_path, &haywardnag, types);
        if (status != 0) {
            goto cleanup;
        }
    }

    haywardnag.details.button_details.text = strdup("Toggle details");
    haywardnag.details.button_details.type = HAYWARDNAG_ACTION_EXPAND;

    if (argc > 1) {
        struct haywardnag_type *type_args = haywardnag_type_new("<args>");
        list_add(types, type_args);

        status = haywardnag_parse_options(argc, argv, &haywardnag, types, type_args, NULL, NULL);
        if (status != 0) {
            goto cleanup;
        }
    }

    if (!haywardnag.message) {
        hwd_log(HWD_ERROR, "No message passed. Please provide --message/-m");
        status = EXIT_FAILURE;
        goto cleanup;
    }

    if (!haywardnag.type) {
        haywardnag.type = haywardnag_type_get(types, "error");
    }

    // Construct a new type with the defaults as the base, the general config
    // on top of that, followed by the type config, and finally any command
    // line arguments
    struct haywardnag_type *type = haywardnag_type_new(haywardnag.type->name);
    haywardnag_type_merge(type, haywardnag_type_get(types, "<defaults>"));
    haywardnag_type_merge(type, haywardnag_type_get(types, "<config>"));
    haywardnag_type_merge(type, haywardnag.type);
    haywardnag_type_merge(type, haywardnag_type_get(types, "<args>"));
    haywardnag.type = type;

    haywardnag_types_free(types);

    struct haywardnag_button button_close = {0};
    button_close.text = strdup("X");
    button_close.type = HAYWARDNAG_ACTION_DISMISS;
    list_add(haywardnag.buttons, &button_close);

    if (haywardnag.details.message) {
        list_add(haywardnag.buttons, &haywardnag.details.button_details);
    }

    hwd_log(HWD_DEBUG, "Output: %s", haywardnag.type->output);
    hwd_log(HWD_DEBUG, "Anchors: %" PRIu32, haywardnag.type->anchors);
    hwd_log(HWD_DEBUG, "Type: %s", haywardnag.type->name);
    hwd_log(HWD_DEBUG, "Message: %s", haywardnag.message);
    char *font = pango_font_description_to_string(haywardnag.type->font_description);
    hwd_log(HWD_DEBUG, "Font: %s", font);
    free(font);
    hwd_log(HWD_DEBUG, "Buttons");
    for (int i = 0; i < haywardnag.buttons->length; i++) {
        struct haywardnag_button *button = haywardnag.buttons->items[i];
        hwd_log(HWD_DEBUG, "\t[%s] `%s`", button->text, button->action);
    }

    signal(SIGTERM, sig_handler);

    haywardnag_setup(&haywardnag);
    haywardnag_run(&haywardnag);
    return status;

cleanup:
    haywardnag_types_free(types);
    free(haywardnag.details.button_details.text);
    haywardnag_destroy(&haywardnag);
    return status;
}
