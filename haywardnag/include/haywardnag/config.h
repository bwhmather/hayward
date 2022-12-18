#ifndef _HAYWARDNAG_CONFIG_H
#define _HAYWARDNAG_CONFIG_H
#include "hayward-common/list.h"

#include "haywardnag/haywardnag.h"

int
haywardnag_parse_options(
    int argc, char **argv, struct haywardnag *haywardnag, list_t *types,
    struct haywardnag_type *type, char **config, bool *debug
);

char *
haywardnag_get_config_path(void);

int
haywardnag_load_config(
    char *path, struct haywardnag *haywardnag, list_t *types
);

#endif
