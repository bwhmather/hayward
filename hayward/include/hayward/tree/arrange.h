#ifndef HWD_TREE_ARRANGE_H
#define HWD_TREE_ARRANGE_H

struct hwd_window;
struct hwd_column;
struct hwd_output;
struct hwd_workspace;
struct hwd_root;

void
arrange_window(struct hwd_window *window);

void
arrange_column(struct hwd_column *column);

void
arrange_workspace(struct hwd_workspace *workspace);

void
arrange_output(struct hwd_output *output);

void
arrange_root(struct hwd_root *root);

#endif
