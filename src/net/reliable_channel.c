#include <stdlib.h>
#include <string.h>

#include "ferrum/net/reliable_channel.h"

static int sequence_more_recent(uint16_t a, uint16_t b) {
    return (uint16_t)(a - b) < 32768u;
}

static int find_sequence_index(const net_reliable_channel_t *channel, uint16_t sequence, size_t *out_index) {
    for (size_t i = 0u; i < channel->slot_count; ++i) {
        if (channel->occupied[i] && channel->sequences[i] == sequence) {
            *out_index = i;
            return 0;
        }
    }
    return -1;
}

void net_reliable_channel_init(net_reliable_channel_t *channel,
                               size_t slot_count,
                               size_t max_payload_size) {
    if (!channel) {
        return;
    }

    channel->payloads = NULL;
    channel->sizes = NULL;
    channel->sequences = NULL;
    channel->occupied = NULL;
    channel->slot_count = 0u;
    channel->max_payload_size = 0u;
    channel->count = 0u;
    channel->next_send_sequence = 0u;
    channel->next_receive_sequence = 0u;
    channel->initialized = 0u;

    if (slot_count == 0u || max_payload_size == 0u) {
        return;
    }
    if (max_payload_size > SIZE_MAX / slot_count) {
        return;
    }

    channel->payloads = (uint8_t *)calloc(slot_count, max_payload_size);
    channel->sizes = (size_t *)calloc(slot_count, sizeof(size_t));
    channel->sequences = (uint16_t *)calloc(slot_count, sizeof(uint16_t));
    channel->occupied = (uint8_t *)calloc(slot_count, sizeof(uint8_t));
    if (!channel->payloads || !channel->sizes || !channel->sequences || !channel->occupied) {
        net_reliable_channel_destroy(channel);
        return;
    }

    channel->slot_count = slot_count;
    channel->max_payload_size = max_payload_size;
    channel->initialized = 1u;
}

void net_reliable_channel_destroy(net_reliable_channel_t *channel) {
    if (!channel) {
        return;
    }
    free(channel->payloads);
    free(channel->sizes);
    free(channel->sequences);
    free(channel->occupied);
    channel->payloads = NULL;
    channel->sizes = NULL;
    channel->sequences = NULL;
    channel->occupied = NULL;
    channel->slot_count = 0u;
    channel->max_payload_size = 0u;
    channel->count = 0u;
    channel->next_send_sequence = 0u;
    channel->next_receive_sequence = 0u;
    channel->initialized = 0u;
}

int net_reliable_channel_send(net_reliable_channel_t *channel,
                              const void *payload,
                              size_t payload_size) {
    if (!channel) {
        return NET_RELIABLE_ERR_INVALID;
    }
    uint16_t sequence = channel->next_send_sequence;
    int rc = net_reliable_channel_send_sequence(channel, sequence, payload, payload_size);
    if (rc == NET_RELIABLE_OK) {
        channel->next_send_sequence = (uint16_t)(channel->next_send_sequence + 1u);
    }
    return rc;
}

int net_reliable_channel_send_sequence(net_reliable_channel_t *channel,
                                       uint16_t sequence,
                                       const void *payload,
                                       size_t payload_size) {
    if (!channel || !channel->initialized || (!payload && payload_size > 0u)) {
        return NET_RELIABLE_ERR_INVALID;
    }
    if (payload_size > channel->max_payload_size) {
        return NET_RELIABLE_ERR_INVALID;
    }
    if (channel->count >= channel->slot_count) {
        return NET_RELIABLE_ERR_FULL;
    }
    if (find_sequence_index(channel, sequence, &(size_t){0}) == 0) {
        return NET_RELIABLE_ERR_FULL;
    }

    size_t slot_index = SIZE_MAX;
    for (size_t i = 0u; i < channel->slot_count; ++i) {
        if (!channel->occupied[i]) {
            slot_index = i;
            break;
        }
    }
    if (slot_index == SIZE_MAX) {
        return NET_RELIABLE_ERR_FULL;
    }

    if (payload_size > 0u) {
        memcpy(channel->payloads + slot_index * channel->max_payload_size, payload, payload_size);
    }
    channel->sizes[slot_index] = payload_size;
    channel->sequences[slot_index] = sequence;
    channel->occupied[slot_index] = 1u;
    channel->count++;
    return NET_RELIABLE_OK;
}

int net_reliable_channel_resend(net_reliable_channel_t *channel, uint16_t sequence) {
    if (!channel || !channel->initialized) {
        return NET_RELIABLE_ERR_INVALID;
    }
    size_t index = 0u;
    if (find_sequence_index(channel, sequence, &index) != 0) {
        return NET_RELIABLE_ERR_NOT_FOUND;
    }
    return NET_RELIABLE_OK;
}

int net_reliable_channel_receive(net_reliable_channel_t *channel,
                                 void *out_payload,
                                 size_t out_capacity,
                                 size_t *out_size) {
    if (!channel || !channel->initialized || !out_payload || !out_size) {
        return NET_RELIABLE_ERR_INVALID;
    }
    if (channel->count == 0u) {
        return NET_RELIABLE_EMPTY;
    }

    size_t index = 0u;
    if (find_sequence_index(channel, channel->next_receive_sequence, &index) != 0) {
        return NET_RELIABLE_EMPTY;
    }

    size_t payload_size = channel->sizes[index];
    if (payload_size > out_capacity) {
        return NET_RELIABLE_ERR_INVALID;
    }
    if (payload_size > 0u) {
        memcpy(out_payload, channel->payloads + index * channel->max_payload_size, payload_size);
    }
    *out_size = payload_size;
    channel->occupied[index] = 0u;
    channel->sizes[index] = 0u;
    channel->count--;

    channel->next_receive_sequence = (uint16_t)(channel->next_receive_sequence + 1u);

    for (;;) {
        size_t next_index = 0u;
        if (find_sequence_index(channel, channel->next_receive_sequence, &next_index) != 0) {
            break;
        }
        if (sequence_more_recent(channel->next_receive_sequence, channel->next_send_sequence)) {
            break;
        }
        break;
    }

    return NET_RELIABLE_OK;
}
