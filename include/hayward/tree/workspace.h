#ifndef _HAYWARD_WORKSPACE_H
#define _HAYWARD_WORKSPACE_H

#include <stdbool.h>
#include "hayward/tree/column.h"
#include "hayward/tree/window.h"
#include "hayward/tree/node.h"

struct hayward_view;

struct hayward_workspace_state {
	struct hayward_window *fullscreen;
	double x, y;
	int width, height;
	struct hayward_output *output; // NULL if no outputs are connected
	list_t *floating;           // struct hayward_window
	list_t *tiling;             // struct hayward_column

	// The column that should be given focus if this workspace is focused.  If
	// the workspace has columns but active_column is NULL, it indicates that
	// the floating layer should be focused.
	struct hayward_column *active_column;
	bool focused;
};

struct hayward_workspace {
	struct hayward_node node;

	char *name;

	struct side_gaps current_gaps;
	int gaps_inner;
	struct side_gaps gaps_outer;

	list_t *output_priority;
	bool urgent;

	struct hayward_workspace_state current;
	struct hayward_workspace_state pending;
};

struct workspace_config *workspace_find_config(const char *workspace_name);

struct hayward_output *workspace_get_initial_output(const char *name);

struct hayward_workspace *workspace_create(struct hayward_output *output,
		const char *name);

void workspace_destroy(struct hayward_workspace *workspace);

void workspace_begin_destroy(struct hayward_workspace *workspace);

void workspace_consider_destroy(struct hayward_workspace *workspace);

char *workspace_next_name(const char *output_name);

struct hayward_workspace *workspace_auto_back_and_forth(
		struct hayward_workspace *workspace);

bool workspace_switch(struct hayward_workspace *workspace);

struct hayward_workspace *workspace_by_number(const char* name);

struct hayward_workspace *workspace_by_name(const char*);

struct hayward_workspace *workspace_output_next(struct hayward_workspace *current);

struct hayward_workspace *workspace_next(struct hayward_workspace *current);

struct hayward_workspace *workspace_output_prev(struct hayward_workspace *current);

struct hayward_workspace *workspace_prev(struct hayward_workspace *current);

bool workspace_is_visible(struct hayward_workspace *workspace);

bool workspace_is_empty(struct hayward_workspace *workspace);

void workspace_output_raise_priority(struct hayward_workspace *workspace,
		struct hayward_output *old_output, struct hayward_output *new_output);

void workspace_output_add_priority(struct hayward_workspace *workspace,
		struct hayward_output *output);

struct hayward_output *workspace_output_get_highest_available(
		struct hayward_workspace *workspace, struct hayward_output *exclude);

void workspace_detect_urgent(struct hayward_workspace *workspace);

void workspace_for_each_window(struct hayward_workspace *workspace, void (*f)(struct hayward_window *container, void *data), void *data);
void workspace_for_each_column(struct hayward_workspace *workspace, void (*f)(struct hayward_column *container, void *data), void *data);

struct hayward_window *workspace_find_window(struct hayward_workspace *workspace,
		bool (*test)(struct hayward_window *window, void *data), void *data);

/**
 * Wrap the workspace's tiling children in a new container.
 * The new container will be the only direct tiling child of the workspace.
 * The new container is returned.
 */
void workspace_detach(struct hayward_workspace *workspace);

struct hayward_column *workspace_add_tiling(struct hayward_workspace *workspace,
		struct hayward_column *column);

void workspace_add_floating(struct hayward_workspace *workspace,
		struct hayward_window *container);

/**
 * Adds a tiling container to the workspace without considering
 * the workspace_layout, so the container will not be split.
 */
void workspace_insert_tiling_direct(struct hayward_workspace *workspace,
		struct hayward_column *column, int index);

struct hayward_column *workspace_insert_tiling(struct hayward_workspace *workspace,
		struct hayward_column *column, int index);

void workspace_remove_gaps(struct hayward_workspace *workspace);

void workspace_add_gaps(struct hayward_workspace *workspace);

struct hayward_window *workspace_split(struct hayward_workspace *workspace,
		enum hayward_column_layout layout);

void workspace_update_representation(struct hayward_workspace *workspace);

void workspace_get_box(struct hayward_workspace *workspace, struct wlr_box *box);

size_t workspace_num_tiling_views(struct hayward_workspace *workspace);

size_t workspace_num_sticky_containers(struct hayward_workspace *workspace);

#endif
