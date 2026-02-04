/**
 * Topic channel ring buffer for decoded messages.
 */
#ifndef FERRUM_NET_TOPIC_CHANNEL_H
#define FERRUM_NET_TOPIC_CHANNEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/** Configuration for a topic channel. */
typedef struct fr_topic_channel_config_t {
    /** Max messages buffered; 0 -> default 64. */
    uint32_t capacity;

    /** Ring buffer capacity in bytes; 0 -> default (64 KiB). */
    uint32_t capacity_bytes;

    /** Max allowed message size in bytes; 0 -> default (1024). */
    uint32_t max_message_size;

    /** Backpressure policy (see FR_TOPIC_BACKPRESSURE_*). */
    uint32_t backpressure;
} fr_topic_channel_config_t;

/** Backpressure policy: fail push (non-blocking). */
#define FR_TOPIC_BACKPRESSURE_FAIL 0u
/** Backpressure policy: drop oldest messages until the new message fits. */
#define FR_TOPIC_BACKPRESSURE_DROP_OLDEST 1u
/** Backpressure policy: drop the new message (push returns false). */
#define FR_TOPIC_BACKPRESSURE_DROP_NEWEST 2u

/** Opaque topic channel handle. */
typedef struct fr_topic_channel fr_topic_channel_t;

/** Create a topic channel with the given config. Returns NULL on failure. */
fr_topic_channel_t *fr_topic_channel_create(const fr_topic_channel_config_t *cfg);

/** Destroy a topic channel and free buffered messages. */
void fr_topic_channel_destroy(fr_topic_channel_t *ch);

/** Push a message into the channel. Returns false if full or allocation failed. */
bool fr_topic_channel_push(fr_topic_channel_t *ch, const uint8_t *data, size_t len);

/** Pop the next message. Writes up to *inout_len bytes into out. Returns false if empty. */
bool fr_topic_channel_pop(fr_topic_channel_t *ch, uint8_t *out, size_t *inout_len);

/** Returns the total number of messages dropped due to backpressure. */
uint64_t fr_topic_channel_stat_dropped(const fr_topic_channel_t *ch);

#endif /* FERRUM_NET_TOPIC_CHANNEL_H */
