#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

struct cmd_results *
cmd_nop(int argc, char **argv) {
    return cmd_results_new(CMD_SUCCESS, NULL);
}
