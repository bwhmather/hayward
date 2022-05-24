#ifndef _WMIIVNAG_CONFIG_H
#define _WMIIVNAG_CONFIG_H
#include "wmiivnag/wmiivnag.h"
#include "list.h"

int wmiivnag_parse_options(int argc, char **argv, struct wmiivnag *wmiivnag,
		list_t *types, struct wmiivnag_type *type, char **config, bool *debug);

char *wmiivnag_get_config_path(void);

int wmiivnag_load_config(char *path, struct wmiivnag *wmiivnag, list_t *types);

#endif
