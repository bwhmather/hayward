#ifndef _WMIIV_WORKSPACE_H
#define _WMIIV_WORKSPACE_H

#include <stdbool.h>
#include "wmiiv/tree/container.h"
#include "wmiiv/tree/node.h"

struct wmiiv_view;

struct wmiiv_workspace_state {
	struct wmiiv_container *fullscreen;
	double x, y;
	int width, height;
	struct wmiiv_output *output;
	list_t *floating;
	list_t *tiling;

	struct wmiiv_container *focused_inactive_child;
	bool focused;
};

struct wmiiv_workspace {
	struct wmiiv_node node;
	struct wmiiv_container *fullscreen;

	char *name;
	char *representation;

	double x, y;
	int width, height;

	struct side_gaps current_gaps;
	int gaps_inner;
	struct side_gaps gaps_outer;

	struct wmiiv_output *output; // NULL if no outputs are connected
	list_t *floating;           // struct wmiiv_container
	list_t *tiling;             // struct wmiiv_container
	list_t *output_priority;
	bool urgent;

	struct wmiiv_workspace_state current;
};

struct workspace_config *workspace_find_config(const char *workspace_name);

struct wmiiv_output *workspace_get_initial_output(const char *name);

struct wmiiv_workspace *workspace_create(struct wmiiv_output *output,
		const char *name);

void workspace_destroy(struct wmiiv_workspace *workspace);

void workspace_begin_destroy(struct wmiiv_workspace *workspace);

void workspace_consider_destroy(struct wmiiv_workspace *workspace);

char *workspace_next_name(const char *output_name);

struct wmiiv_workspace *workspace_auto_back_and_forth(
		struct wmiiv_workspace *workspace);

bool workspace_switch(struct wmiiv_workspace *workspace);

struct wmiiv_workspace *workspace_by_number(const char* name);

struct wmiiv_workspace *workspace_by_name(const char*);

struct wmiiv_workspace *workspace_output_next(struct wmiiv_workspace *current);

struct wmiiv_workspace *workspace_next(struct wmiiv_workspace *current);

struct wmiiv_workspace *workspace_output_prev(struct wmiiv_workspace *current);

struct wmiiv_workspace *workspace_prev(struct wmiiv_workspace *current);

bool workspace_is_visible(struct wmiiv_workspace *workspace);

bool workspace_is_empty(struct wmiiv_workspace *workspace);

void workspace_output_raise_priority(struct wmiiv_workspace *workspace,
		struct wmiiv_output *old_output, struct wmiiv_output *new_output);

void workspace_output_add_priority(struct wmiiv_workspace *workspace,
		struct wmiiv_output *output);

struct wmiiv_output *workspace_output_get_highest_available(
		struct wmiiv_workspace *workspace, struct wmiiv_output *exclude);

void workspace_detect_urgent(struct wmiiv_workspace *workspace);

void workspace_for_each_container(struct wmiiv_workspace *workspace,
		void (*f)(struct wmiiv_container *container, void *data), void *data);

struct wmiiv_container *workspace_find_container(struct wmiiv_workspace *workspace,
		bool (*test)(struct wmiiv_container *container, void *data), void *data);

/**
 * Wrap the workspace's tiling children in a new container.
 * The new container will be the only direct tiling child of the workspace.
 * The new container is returned.
 */
void workspace_detach(struct wmiiv_workspace *workspace);

struct wmiiv_container *workspace_add_tiling(struct wmiiv_workspace *workspace,
		struct wmiiv_container *container);

void workspace_add_floating(struct wmiiv_workspace *workspace,
		struct wmiiv_container *container);

/**
 * Adds a tiling container to the workspace without considering
 * the workspace_layout, so the container will not be split.
 */
void workspace_insert_tiling_direct(struct wmiiv_workspace *workspace,
		struct wmiiv_container *container, int index);

struct wmiiv_container *workspace_insert_tiling(struct wmiiv_workspace *workspace,
		struct wmiiv_container *container, int index);

void workspace_remove_gaps(struct wmiiv_workspace *workspace);

void workspace_add_gaps(struct wmiiv_workspace *workspace);

struct wmiiv_container *workspace_split(struct wmiiv_workspace *workspace,
		enum wmiiv_container_layout layout);

void workspace_update_representation(struct wmiiv_workspace *workspace);

void workspace_get_box(struct wmiiv_workspace *workspace, struct wlr_box *box);

size_t workspace_num_tiling_views(struct wmiiv_workspace *workspace);

size_t workspace_num_sticky_containers(struct wmiiv_workspace *workspace);

#endif