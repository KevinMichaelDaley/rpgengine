#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "ferrum/net/topic_channel.h"
#include "channel_internal.h"

static uint32_t fr_topic_ring_free_bytes(const fr_topic_channel_t *ch) {
    return ch->ring_capacity_bytes - ch->used_bytes;
}

static void fr_topic_ring_write_bytes(fr_topic_channel_t *ch, const void *src, uint32_t len) {
    const uint32_t first = ch->ring_capacity_bytes - ch->tail;
    if (len <= first) {
        memcpy(&ch->ring[ch->tail], src, len);
        ch->tail += len;
        if (ch->tail == ch->ring_capacity_bytes) ch->tail = 0u;
        return;
    }

    memcpy(&ch->ring[ch->tail], src, first);
    memcpy(&ch->ring[0], (const uint8_t *)src + first, len - first);
    ch->tail = len - first;
}

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

static void fr_topic_ring_consume_bytes(fr_topic_channel_t *ch, uint32_t len) {
    ch->head += len;
    ch->head %= ch->ring_capacity_bytes;
}

static void fr_topic_channel_discard_one_locked(fr_topic_channel_t *ch) {
    if (ch->count == 0u) return;

    uint32_t msg_len = 0u;
    if (!fr_topic_ring_peek_u32(ch, &msg_len)) return;

    const uint32_t needed = 4u + msg_len;
    if (needed > ch->used_bytes) return;

    fr_topic_ring_consume_bytes(ch, needed);
    ch->used_bytes -= needed;
    ch->count -= 1u;
    (void)atomic_fetch_add(&ch->stat_dropped, 1u);
}

bool fr_topic_channel_push(fr_topic_channel_t *ch, const uint8_t *data, size_t len) {
    if (!ch || !data || len == 0u) return false;
    if (len > (size_t)UINT32_MAX) {
        (void)atomic_fetch_add(&ch->stat_dropped, 1u);
        return false;
    }

    const uint32_t msg_len = (uint32_t)len;

    /* Guard against uint32 overflow: 4 + msg_len must not wrap. */
    if (msg_len > UINT32_MAX - 4u) {
        (void)atomic_fetch_add(&ch->stat_dropped, 1u);
        return false;
    }

    job_spinlock_lock(&ch->lock);
    if (msg_len > ch->max_message_size) {
        (void)atomic_fetch_add(&ch->stat_dropped, 1u);
        job_spinlock_unlock(&ch->lock);
        return false;
    }

    const uint32_t needed = 4u + msg_len;
    if (needed > ch->ring_capacity_bytes) {
        (void)atomic_fetch_add(&ch->stat_dropped, 1u);
        job_spinlock_unlock(&ch->lock);
        return false;
    }

    if (ch->backpressure == FR_TOPIC_BACKPRESSURE_DROP_OLDEST) {
        while ((ch->count >= ch->message_capacity || fr_topic_ring_free_bytes(ch) < needed) && ch->count > 0u) {
            fr_topic_channel_discard_one_locked(ch);
        }
    }

    if (ch->count >= ch->message_capacity || fr_topic_ring_free_bytes(ch) < needed) {
        (void)atomic_fetch_add(&ch->stat_dropped, 1u);
        job_spinlock_unlock(&ch->lock);
        return false;
    }

    fr_topic_ring_write_bytes(ch, &msg_len, 4u);
    fr_topic_ring_write_bytes(ch, data, msg_len);
    ch->used_bytes += needed;
    ch->count += 1u;
    job_spinlock_unlock(&ch->lock);
    return true;
}
