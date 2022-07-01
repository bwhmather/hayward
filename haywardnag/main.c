#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <signal.h>
#include "log.h"
#include "list.h"
#include "haywardnag/config.h"
#include "haywardnag/haywardnag.h"
#include "haywardnag/types.h"

static struct haywardnag haywardnag;

void sig_handler(int signal) {
	haywardnag_destroy(&haywardnag);
	exit(EXIT_FAILURE);
}

void hayward_terminate(int code) {
	haywardnag_destroy(&haywardnag);
	exit(code);
}

int main(int argc, char **argv) {
	int status = EXIT_SUCCESS;

	list_t *types = create_list();
	haywardnag_types_add_default(types);

	haywardnag.buttons = create_list();
	wl_list_init(&haywardnag.outputs);
	wl_list_init(&haywardnag.seats);

	char *config_path = NULL;
	bool debug = false;
	status = haywardnag_parse_options(argc, argv, NULL, NULL, NULL,
			&config_path, &debug);
	if (status != 0)  {
		goto cleanup;
	}
	hayward_log_init(debug ? HAYWARD_DEBUG : HAYWARD_ERROR, NULL);

	if (!config_path) {
		config_path = haywardnag_get_config_path();
	}
	if (config_path) {
		hayward_log(HAYWARD_DEBUG, "Loading config file: %s", config_path);
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

		status = haywardnag_parse_options(argc, argv, &haywardnag, types,
				type_args, NULL, NULL);
		if (status != 0) {
			goto cleanup;
		}
	}

	if (!haywardnag.message) {
		hayward_log(HAYWARD_ERROR, "No message passed. Please provide --message/-m");
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

	struct haywardnag_button button_close = { 0 };
	button_close.text = strdup("X");
	button_close.type = HAYWARDNAG_ACTION_DISMISS;
	list_add(haywardnag.buttons, &button_close);

	if (haywardnag.details.message) {
		list_add(haywardnag.buttons, &haywardnag.details.button_details);
	}

	hayward_log(HAYWARD_DEBUG, "Output: %s", haywardnag.type->output);
	hayward_log(HAYWARD_DEBUG, "Anchors: %" PRIu32, haywardnag.type->anchors);
	hayward_log(HAYWARD_DEBUG, "Type: %s", haywardnag.type->name);
	hayward_log(HAYWARD_DEBUG, "Message: %s", haywardnag.message);
	hayward_log(HAYWARD_DEBUG, "Font: %s", haywardnag.type->font);
	hayward_log(HAYWARD_DEBUG, "Buttons");
	for (int i = 0; i < haywardnag.buttons->length; i++) {
		struct haywardnag_button *button = haywardnag.buttons->items[i];
		hayward_log(HAYWARD_DEBUG, "\t[%s] `%s`", button->text, button->action);
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
