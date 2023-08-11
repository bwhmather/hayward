#ifndef __HWDOUT_UTIL_H__
#define __HWDOUT_UTIL_H__

#include <gio/gio.h>

G_BEGIN_DECLS

void
hwdout_copy_list_store(GListStore *tgt_store, GListStore *src_store);

G_END_DECLS

#endif /* __HWDOUT_UTIL_H__ */
