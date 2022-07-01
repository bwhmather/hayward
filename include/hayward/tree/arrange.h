#ifndef _HAYWARD_ARRANGE_H
#define _HAYWARD_ARRANGE_H

struct hayward_output;
struct hayward_workspace;
struct hayward_column;
struct hayward_window;
struct hayward_node;

void arrange_root(void);

void arrange_output(struct hayward_output *output);

void arrange_node(struct hayward_node *node);

void arrange_workspace(struct hayward_workspace *workspace);

void arrange_column(struct hayward_column *column);

void arrange_window(struct hayward_window *window);

#endif
