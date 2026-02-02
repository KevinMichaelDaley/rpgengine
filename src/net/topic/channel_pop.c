#include <string.h>
#include <stdlib.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

bool fr_topic_channel_pop(fr_topic_channel_t *ch, uint8_t *out, size_t *inout_len) {
    if (!ch || !out || !inout_len) return false;
    if (atomic_load(&ch->count) == 0u) return false;
    uint32_t head = atomic_load(&ch->head);
    fr_topic_item *it = &ch->items[head];
    if (!it->data) return false;
    if (it->len > *inout_len) return false;
    memcpy(out, it->data, it->len);
    *inout_len = it->len;
    free(it->data);
    it->data = NULL;
    it->len = 0;
    head = (head + 1u) % ch->capacity;
    atomic_store(&ch->head, head);
    atomic_fetch_sub(&ch->count, 1u);
    return true;
}
