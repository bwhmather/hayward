#include "hayward-common/log.h"
#include "hayward/commands.h"
#include "hayward/config.h"

struct cmd_results *cmd_new_float(int argc, char **argv) {
	hayward_log(HAYWARD_INFO, "Warning: new_float is deprecated. "
		"Use default_floating_border instead.");
	if (config->reading) {
		config_add_haywardnag_warning("new_float is deprecated. "
			"Use default_floating_border instead.");
	}
	return cmd_default_floating_border(argc, argv);
}
