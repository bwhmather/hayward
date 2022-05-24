#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/config.h"

struct cmd_results *cmd_new_window(int argc, char **argv) {
	wmiiv_log(SWAY_INFO, "Warning: new_window is deprecated. "
		"Use default_border instead.");
	if (config->reading) {
		config_add_wmiivnag_warning("new_window is deprecated. "
			"Use default_border instead.");
	}
	return cmd_default_border(argc, argv);
}
