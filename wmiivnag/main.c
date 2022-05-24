#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <signal.h>
#include "log.h"
#include "list.h"
#include "wmiivnag/config.h"
#include "wmiivnag/wmiivnag.h"
#include "wmiivnag/types.h"

static struct wmiivnag wmiivnag;

void sig_handler(int signal) {
	wmiivnag_destroy(&wmiivnag);
	exit(EXIT_FAILURE);
}

void wmiiv_terminate(int code) {
	wmiivnag_destroy(&wmiivnag);
	exit(code);
}

int main(int argc, char **argv) {
	int status = EXIT_SUCCESS;

	list_t *types = create_list();
	wmiivnag_types_add_default(types);

	wmiivnag.buttons = create_list();
	wl_list_init(&wmiivnag.outputs);
	wl_list_init(&wmiivnag.seats);

	char *config_path = NULL;
	bool debug = false;
	status = wmiivnag_parse_options(argc, argv, NULL, NULL, NULL,
			&config_path, &debug);
	if (status != 0)  {
		goto cleanup;
	}
	wmiiv_log_init(debug ? SWAY_DEBUG : SWAY_ERROR, NULL);

	if (!config_path) {
		config_path = wmiivnag_get_config_path();
	}
	if (config_path) {
		wmiiv_log(SWAY_DEBUG, "Loading config file: %s", config_path);
		status = wmiivnag_load_config(config_path, &wmiivnag, types);
		if (status != 0) {
			goto cleanup;
		}
	}

	wmiivnag.details.button_details.text = strdup("Toggle details");
	wmiivnag.details.button_details.type = SWAYNAG_ACTION_EXPAND;

	if (argc > 1) {
		struct wmiivnag_type *type_args = wmiivnag_type_new("<args>");
		list_add(types, type_args);

		status = wmiivnag_parse_options(argc, argv, &wmiivnag, types,
				type_args, NULL, NULL);
		if (status != 0) {
			goto cleanup;
		}
	}

	if (!wmiivnag.message) {
		wmiiv_log(SWAY_ERROR, "No message passed. Please provide --message/-m");
		status = EXIT_FAILURE;
		goto cleanup;
	}

	if (!wmiivnag.type) {
		wmiivnag.type = wmiivnag_type_get(types, "error");
	}

	// Construct a new type with the defaults as the base, the general config
	// on top of that, followed by the type config, and finally any command
	// line arguments
	struct wmiivnag_type *type = wmiivnag_type_new(wmiivnag.type->name);
	wmiivnag_type_merge(type, wmiivnag_type_get(types, "<defaults>"));
	wmiivnag_type_merge(type, wmiivnag_type_get(types, "<config>"));
	wmiivnag_type_merge(type, wmiivnag.type);
	wmiivnag_type_merge(type, wmiivnag_type_get(types, "<args>"));
	wmiivnag.type = type;

	wmiivnag_types_free(types);

	struct wmiivnag_button button_close = { 0 };
	button_close.text = strdup("X");
	button_close.type = SWAYNAG_ACTION_DISMISS;
	list_add(wmiivnag.buttons, &button_close);

	if (wmiivnag.details.message) {
		list_add(wmiivnag.buttons, &wmiivnag.details.button_details);
	}

	wmiiv_log(SWAY_DEBUG, "Output: %s", wmiivnag.type->output);
	wmiiv_log(SWAY_DEBUG, "Anchors: %" PRIu32, wmiivnag.type->anchors);
	wmiiv_log(SWAY_DEBUG, "Type: %s", wmiivnag.type->name);
	wmiiv_log(SWAY_DEBUG, "Message: %s", wmiivnag.message);
	wmiiv_log(SWAY_DEBUG, "Font: %s", wmiivnag.type->font);
	wmiiv_log(SWAY_DEBUG, "Buttons");
	for (int i = 0; i < wmiivnag.buttons->length; i++) {
		struct wmiivnag_button *button = wmiivnag.buttons->items[i];
		wmiiv_log(SWAY_DEBUG, "\t[%s] `%s`", button->text, button->action);
	}

	signal(SIGTERM, sig_handler);

	wmiivnag_setup(&wmiivnag);
	wmiivnag_run(&wmiivnag);
	return status;

cleanup:
	wmiivnag_types_free(types);
	free(wmiivnag.details.button_details.text);
	wmiivnag_destroy(&wmiivnag);
	return status;
}
