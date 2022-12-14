#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include "haywardbar/bar.h"
#include "hayward-common/ipc-client.h"
#include "hayward-common/log.h"

static struct haywardbar haywardbar;

void sig_handler(int signal) {
	haywardbar.running = false;
}

int main(int argc, char **argv) {
	char *socket_path = NULL;
	bool debug = false;

	static const struct option long_options[] = {
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'v'},
		{"socket", required_argument, NULL, 's'},
		{"bar_id", required_argument, NULL, 'b'},
		{"debug", no_argument, NULL, 'd'},
		{0, 0, 0, 0}
	};

	const char *usage =
		"Usage: haywardbar [options...]\n"
		"\n"
		"  -h, --help             Show help message and quit.\n"
		"  -v, --version          Show the version number and quit.\n"
		"  -s, --socket <socket>  Connect to hayward via socket.\n"
		"  -b, --bar_id <id>      Bar ID for which to get the configuration.\n"
		"  -d, --debug            Enable debugging.\n"
		"\n"
		" PLEASE NOTE that haywardbar will be automatically started by hayward as\n"
		" soon as there is a 'bar' configuration block in your config file.\n"
		" You should never need to start it manually.\n";

	int c;
	while (1) {
		int option_index = 0;
		c = getopt_long(argc, argv, "hvs:b:d", long_options, &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 's': // Socket
			socket_path = strdup(optarg);
			break;
		case 'b': // Type
			haywardbar.id = strdup(optarg);
			break;
		case 'v':
			printf("haywardbar version " HAYWARD_VERSION "\n");
			exit(EXIT_SUCCESS);
			break;
		case 'd': // Debug
			debug = true;
			break;
		default:
			fprintf(stderr, "%s", usage);
			exit(EXIT_FAILURE);
		}
	}

	if (debug) {
		hayward_log_init(HAYWARD_DEBUG);
	} else {
		hayward_log_init(HAYWARD_INFO);
	}

	if (!haywardbar.id) {
		hayward_log(HAYWARD_ERROR, "No bar_id passed. "
				"Provide --bar_id or let hayward start haywardbar");
		return 1;
	}

	if (!socket_path) {
		socket_path = get_socketpath();
		if (!socket_path) {
			hayward_log(HAYWARD_ERROR, "Unable to retrieve socket path");
			return 1;
		}
	}

	if (!bar_setup(&haywardbar, socket_path)) {
		free(socket_path);
		return 1;
	}

	free(socket_path);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	haywardbar.running = true;
	bar_run(&haywardbar);
	bar_teardown(&haywardbar);
	return 0;
}
