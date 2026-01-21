#include <stdlib.h>
#include <string.h>

#include "ferrum/net/unreliable_channel.h"

static uint8_t *channel_slot_ptr(const net_unreliable_channel_t *channel, size_t index) {
    return channel->payloads + index * channel->max_payload_size;
}

void net_unreliable_channel_init(net_unreliable_channel_t *channel,
                                 size_t slot_count,
                                 size_t max_payload_size) {
    if (!channel) {
        return;
    }

    channel->payloads = NULL;
    channel->sizes = NULL;
    channel->slot_count = 0u;
    channel->max_payload_size = 0u;
    channel->head = 0u;
    channel->tail = 0u;
    channel->count = 0u;

    if (slot_count == 0u || max_payload_size == 0u) {
        return;
    }
    if (max_payload_size > SIZE_MAX / slot_count) {
        return;
    }

    channel->payloads = (uint8_t *)calloc(slot_count, max_payload_size);
    channel->sizes = (size_t *)calloc(slot_count, sizeof(size_t));
    if (!channel->payloads || !channel->sizes) {
        net_unreliable_channel_destroy(channel);
        return;
    }

    channel->slot_count = slot_count;
    channel->max_payload_size = max_payload_size;
}

void net_unreliable_channel_destroy(net_unreliable_channel_t *channel) {
    if (!channel) {
        return;
    }
    free(channel->payloads);
    free(channel->sizes);
    channel->payloads = NULL;
    channel->sizes = NULL;
    channel->slot_count = 0u;
    channel->max_payload_size = 0u;
    channel->head = 0u;
    channel->tail = 0u;
    channel->count = 0u;
}

int net_unreliable_channel_send(net_unreliable_channel_t *channel,
                                const void *payload,
                                size_t payload_size) {
    if (!channel || (!payload && payload_size > 0u)) {
        return NET_UNRELIABLE_ERR_INVALID;
    }
    if (!channel->payloads || !channel->sizes || channel->slot_count == 0u) {
        return NET_UNRELIABLE_ERR_INVALID;
    }
    if (payload_size > channel->max_payload_size) {
        return NET_UNRELIABLE_ERR_INVALID;
    }
    if (channel->count >= channel->slot_count) {
        return NET_UNRELIABLE_ERR_FULL;
    }

    if (payload_size > 0u) {
        memcpy(channel_slot_ptr(channel, channel->tail), payload, payload_size);
    }
    channel->sizes[channel->tail] = payload_size;
    channel->tail = (channel->tail + 1u) % channel->slot_count;
    channel->count++;
    return NET_UNRELIABLE_OK;
}

int net_unreliable_channel_receive(net_unreliable_channel_t *channel,
                                   void *out_payload,
                                   size_t out_capacity,
                                   size_t *out_size) {
    if (!channel || !out_payload || !out_size) {
        return NET_UNRELIABLE_ERR_INVALID;
    }
    if (!channel->payloads || !channel->sizes || channel->slot_count == 0u) {
        return NET_UNRELIABLE_ERR_INVALID;
    }
    if (channel->count == 0u) {
        return NET_UNRELIABLE_EMPTY;
    }

    size_t payload_size = channel->sizes[channel->head];
    if (payload_size > out_capacity) {
        return NET_UNRELIABLE_ERR_INVALID;
    }
    if (payload_size > 0u) {
        memcpy(out_payload, channel_slot_ptr(channel, channel->head), payload_size);
    }
    *out_size = payload_size;
    channel->sizes[channel->head] = 0u;
    channel->head = (channel->head + 1u) % channel->slot_count;
    channel->count--;
    return NET_UNRELIABLE_OK;
}
