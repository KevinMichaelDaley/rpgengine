#include <stdlib.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

void fr_topic_channel_destroy(fr_topic_channel_t *ch) {
    if (!ch) return;
    (void)mtx_lock(&ch->lock);
    if (ch->items) {
        for (uint32_t i = 0; i < ch->capacity; ++i) {
            if (ch->items[i].data) free(ch->items[i].data);
        }
        free(ch->items);
    }
    (void)mtx_unlock(&ch->lock);
    mtx_destroy(&ch->lock);
    free(ch);
}
