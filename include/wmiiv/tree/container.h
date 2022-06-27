#ifndef _WMIIV_CONTAINER_H
#define _WMIIV_CONTAINER_H
#include <stdint.h>
#include <sys/types.h>
#include <wlr/types/wlr_compositor.h>
#include "list.h"
#include "wmiiv/tree/node.h"

enum wmiiv_column_layout {
	L_NONE,
	L_VERT,
	L_STACKED,
	L_TABBED,
};

// TODO (wmiiv) Delete whole module
#include "wmiiv/tree/column.h"
#include "wmiiv/tree/window.h"

#endif
