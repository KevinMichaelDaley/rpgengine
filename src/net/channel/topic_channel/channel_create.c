#include <stdlib.h>
#include <string.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

fr_topic_channel_t *fr_topic_channel_create(const fr_topic_channel_config_t *cfg) {
    const uint32_t default_message_capacity = 64u;
    const uint32_t default_ring_capacity_bytes = 64u * 1024u;
    const uint32_t default_max_message_size = 1024u;

    const uint32_t message_capacity = (cfg && cfg->capacity) ? cfg->capacity : default_message_capacity;
    const uint32_t ring_capacity_bytes = (cfg && cfg->capacity_bytes) ? cfg->capacity_bytes : default_ring_capacity_bytes;

    if (ring_capacity_bytes <= 4u) return NULL;

    uint32_t max_message_size = (cfg && cfg->max_message_size) ? cfg->max_message_size : default_max_message_size;
    if (max_message_size > (ring_capacity_bytes - 4u)) {
        max_message_size = ring_capacity_bytes - 4u;
    }

    uint32_t backpressure = FR_TOPIC_BACKPRESSURE_FAIL;
    if (cfg) backpressure = cfg->backpressure;
    if (backpressure != FR_TOPIC_BACKPRESSURE_FAIL && backpressure != FR_TOPIC_BACKPRESSURE_DROP_OLDEST &&
        backpressure != FR_TOPIC_BACKPRESSURE_DROP_NEWEST) {
        backpressure = FR_TOPIC_BACKPRESSURE_FAIL;
    }

    fr_topic_channel_t *ch = (fr_topic_channel_t *)malloc(sizeof(fr_topic_channel_t));
    if (!ch) return NULL;
    memset(ch, 0, sizeof(*ch));

    ch->ring = (uint8_t *)malloc(ring_capacity_bytes);
    if (!ch->ring) {
        free(ch);
        return NULL;
    }
    ch->ring_capacity_bytes = ring_capacity_bytes;
    ch->message_capacity = message_capacity;
    ch->max_message_size = max_message_size;
    ch->backpressure = backpressure;

    if (mtx_init(&ch->lock, mtx_plain) != thrd_success) {
        free(ch->ring);
        free(ch);
        return NULL;
    }

    ch->head = 0u;
    ch->tail = 0u;
    ch->used_bytes = 0u;
    ch->count = 0u;
    atomic_init(&ch->stat_dropped, 0u);
    return ch;
}
