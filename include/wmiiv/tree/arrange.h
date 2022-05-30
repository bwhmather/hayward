#ifndef _WMIIV_ARRANGE_H
#define _WMIIV_ARRANGE_H

struct wmiiv_output;
struct wmiiv_workspace;
struct wmiiv_container;
struct wmiiv_node;

void arrange_root(void);

void arrange_output(struct wmiiv_output *output);

void arrange_node(struct wmiiv_node *node);

void arrange_workspace(struct wmiiv_workspace *workspace);

void arrange_column(struct wmiiv_container *column);

void arrange_window(struct wmiiv_container *window);

#endif
