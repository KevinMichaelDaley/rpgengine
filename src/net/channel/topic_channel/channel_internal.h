#ifndef FERRUM_NET_CHANNEL_TOPIC_CHANNEL_INTERNAL_H
#define FERRUM_NET_CHANNEL_TOPIC_CHANNEL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <threads.h>

typedef struct fr_topic_channel {
    uint8_t *ring;
    uint32_t ring_capacity_bytes;
    uint32_t message_capacity;
    uint32_t max_message_size;
    uint32_t backpressure;

    mtx_t lock;

    uint32_t head;
    uint32_t tail;
    uint32_t used_bytes;
    uint32_t count;

    atomic_uint_least64_t stat_dropped;
} fr_topic_channel_t;

#endif /* FERRUM_NET_CHANNEL_TOPIC_CHANNEL_INTERNAL_H */
