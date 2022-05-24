#include <string.h>
#include "wmiiv/commands.h"
#include "wmiiv/config.h"
#include "wmiiv/tree/arrange.h"
#include "wmiiv/tree/view.h"
#include "wmiiv/tree/container.h"
#include "log.h"
#include "stringop.h"
#include "util.h"

struct cmd_results *cmd_smart_gaps(int argc, char **argv) {
	struct cmd_results *error = checkarg(argc, "smart_gaps", EXPECTED_AT_LEAST, 1);

	if (error) {
		return error;
	}

	if (strcmp(argv[0], "inverse_outer") == 0) {
		config->smart_gaps = SMART_GAPS_INVERSE_OUTER;
	} else {
		config->smart_gaps = parse_boolean(argv[0], config->smart_gaps)
			? SMART_GAPS_ON : SMART_GAPS_OFF;
	}

	arrange_root();

	return cmd_results_new(CMD_SUCCESS, NULL);
}
