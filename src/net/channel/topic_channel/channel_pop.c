#include <string.h>
#include <stdint.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

static bool fr_topic_ring_peek_u32(const fr_topic_channel_t *ch, uint32_t *out_u32) {
    if (ch->used_bytes < 4u) return false;

    uint8_t tmp[4];
    const uint32_t first = ch->ring_capacity_bytes - ch->head;
    if (4u <= first) {
        memcpy(tmp, &ch->ring[ch->head], 4u);
    } else {
        memcpy(tmp, &ch->ring[ch->head], first);
        memcpy(tmp + first, &ch->ring[0], 4u - first);
    }

    uint32_t v = 0u;
    memcpy(&v, tmp, sizeof(v));
    *out_u32 = v;
    return true;
}

static void fr_topic_ring_read_bytes(fr_topic_channel_t *ch, void *dst, uint32_t len) {
    const uint32_t first = ch->ring_capacity_bytes - ch->head;
    if (len <= first) {
        memcpy(dst, &ch->ring[ch->head], len);
        ch->head += len;
        if (ch->head == ch->ring_capacity_bytes) ch->head = 0u;
        return;
    }

    memcpy(dst, &ch->ring[ch->head], first);
    memcpy((uint8_t *)dst + first, &ch->ring[0], len - first);
    ch->head = len - first;
}

bool fr_topic_channel_pop(fr_topic_channel_t *ch, uint8_t *out, size_t *inout_len) {
    if (!ch || !out || !inout_len) return false;

    pthread_mutex_lock(&ch->lock);
    if (ch->count == 0u) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }

    uint32_t msg_len = 0u;
    if (!fr_topic_ring_peek_u32(ch, &msg_len)) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }
    if ((size_t)msg_len > *inout_len) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }
    if (ch->used_bytes < (4u + msg_len)) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }

    fr_topic_ring_read_bytes(ch, &msg_len, 4u);
    fr_topic_ring_read_bytes(ch, out, msg_len);
    *inout_len = (size_t)msg_len;
    ch->used_bytes -= (4u + msg_len);
    ch->count -= 1u;
    pthread_mutex_unlock(&ch->lock);
    return true;
}
