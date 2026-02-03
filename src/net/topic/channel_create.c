#include <stdlib.h>
#include <string.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

fr_topic_channel_t *fr_topic_channel_create(const fr_topic_channel_config_t *cfg) {
    uint32_t cap = (cfg && cfg->capacity) ? cfg->capacity : 64u;
    fr_topic_channel_t *ch = (fr_topic_channel_t *)malloc(sizeof(fr_topic_channel_t));
    if (!ch) return NULL;
    memset(ch, 0, sizeof(*ch));
    ch->items = (fr_topic_item *)calloc(cap, sizeof(fr_topic_item));
    if (!ch->items) { free(ch); return NULL; }
    ch->capacity = cap;
    if (mtx_init(&ch->lock, mtx_plain) != thrd_success) {
        free(ch->items);
        free(ch);
        return NULL;
    }
    atomic_init(&ch->head, 0u);
    atomic_init(&ch->tail, 0u);
    atomic_init(&ch->count, 0u);
    return ch;
}
