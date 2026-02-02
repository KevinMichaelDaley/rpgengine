#include <stdlib.h>
#include <string.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

bool fr_topic_channel_push(fr_topic_channel_t *ch, const uint8_t *data, size_t len) {
    if (!ch || !data || len == 0) return false;
    if (atomic_load(&ch->count) >= ch->capacity) return false;
    uint32_t tail = atomic_load(&ch->tail);
    fr_topic_item *it = &ch->items[tail];
    if (it->data) return false; // should be empty
    it->data = (uint8_t *)malloc(len);
    if (!it->data) return false;
    memcpy(it->data, data, len);
    it->len = len;
    tail = (tail + 1u) % ch->capacity;
    atomic_store(&ch->tail, tail);
    atomic_fetch_add(&ch->count, 1u);
    return true;
}
