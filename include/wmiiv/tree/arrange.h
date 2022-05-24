#ifndef _SWAY_ARRANGE_H
#define _SWAY_ARRANGE_H

struct sway_output;
struct sway_workspace;
struct sway_container;
struct sway_node;

void arrange_root(void);

void arrange_output(struct sway_output *output);

void arrange_node(struct sway_node *node);

void arrange_workspace(struct sway_workspace *workspace);

void arrange_column(struct sway_container *col);

void arrange_window(struct sway_container *win);

#endif
