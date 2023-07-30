#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <config.h>

#include "hayward/commands.h"

#include <stdlib.h>
#include <string.h>

#include <hayward-common/list.h>
#include <hayward-common/stringop.h>

#include <hayward/config.h>

// sort in order of longest->shortest
static int
compare_set_qsort(const void *_l, const void *_r) {
    struct hwd_variable const *l = *(void **)_l;
    struct hwd_variable const *r = *(void **)_r;
    return strlen(r->name) - strlen(l->name);
}

void
free_hwd_variable(struct hwd_variable *var) {
    if (!var) {
        return;
    }
    free(var->name);
    free(var->value);
    free(var);
}

struct cmd_results *
cmd_set(int argc, char **argv) {
    struct cmd_results *error = NULL;
    if ((error = checkarg(argc, "set", EXPECTED_AT_LEAST, 2))) {
        return error;
    }

    if (argv[0][0] != '$') {
        return cmd_results_new(CMD_INVALID, "variable '%s' must start with $", argv[0]);
    }

    struct hwd_variable *var = NULL;
    // Find old variable if it exists
    int i;
    for (i = 0; i < config->symbols->length; ++i) {
        var = config->symbols->items[i];
        if (strcmp(var->name, argv[0]) == 0) {
            break;
        }
        var = NULL;
    }
    if (var) {
        free(var->value);
    } else {
        var = malloc(sizeof(struct hwd_variable));
        if (!var) {
            return cmd_results_new(CMD_FAILURE, "Unable to allocate variable");
        }
        var->name = strdup(argv[0]);
        list_add(config->symbols, var);
        list_qsort(config->symbols, compare_set_qsort);
    }
    var->value = join_args(argv + 1, argc - 1);
    return cmd_results_new(CMD_SUCCESS, NULL);
}
