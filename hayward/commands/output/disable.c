#include "hayward/commands.h"
#include "hayward/config.h"

struct cmd_results *output_cmd_disable(int argc, char **argv) {
	if (!config->handler_context.output_config) {
		return cmd_results_new(CMD_FAILURE, "Missing output config");
	}
	config->handler_context.output_config->enabled = 0;

	config->handler_context.leftovers.argc = argc;
	config->handler_context.leftovers.argv = argv;
	return NULL;
}
