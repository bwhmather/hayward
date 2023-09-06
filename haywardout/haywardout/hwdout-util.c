#include "hwdout-util.h"

#include <gio/gio.h>

/**
 * Updates target store to match source.
 * Optimised assuming that items can be removed from anywhere in the source
 * list, but will only ever be appended.
 */
void
hwdout_copy_list_store(GListStore *tgt_store, GListStore *src_store) {
    guint i = 0;
    while (TRUE) {
        GObject *src_item = g_list_model_get_item(G_LIST_MODEL(src_store), i);
        GObject *tgt_item = g_list_model_get_item(G_LIST_MODEL(tgt_store), i);

        if (src_item == NULL) {
            if (tgt_item != NULL) {
                // Current list has trailing items that should be removed.
                g_list_store_splice(
                    tgt_store, i, g_list_model_get_n_items(G_LIST_MODEL(tgt_store)) - i, NULL, 0
                );
            }
            break;
        }

        if (tgt_item == NULL) {
            // Pending list has new items to append to current list.
            g_list_store_append(tgt_store, src_item);
            i++;
            continue;
        }

        if (src_item != tgt_item) {
            // Mode has been removed in latest version.
            g_list_store_remove(tgt_store, i);
            i++;
            continue;
        }

        // Modes match.  Continue.
        i++;
    }
}
