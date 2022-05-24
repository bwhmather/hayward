#include "log.h"
#include "wmiiv/commands.h"
#include "wmiiv/config.h"

struct cmd_results *cmd_new_float(int argc, char **argv) {
	wmiiv_log(WMIIV_INFO, "Warning: new_float is deprecated. "
		"Use default_floating_border instead.");
	if (config->reading) {
		config_add_wmiivnag_warning("new_float is deprecated. "
			"Use default_floating_border instead.");
	}
	return cmd_default_floating_border(argc, argv);
}
