#ifndef FERRUM_NET_TOPIC_CHANNEL_INTERNAL_H
#define FERRUM_NET_TOPIC_CHANNEL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

typedef struct fr_topic_item {
    uint8_t *data;
    size_t len;
} fr_topic_item;

typedef struct fr_topic_channel {
    fr_topic_item *items;
    uint32_t capacity;
    atomic_uint head;
    atomic_uint tail;
    atomic_uint count;
} fr_topic_channel_t;

#endif /* FERRUM_NET_TOPIC_CHANNEL_INTERNAL_H */
