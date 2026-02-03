#ifndef FR_RUDP_STREAM_INTERNAL_H
#define FR_RUDP_STREAM_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "ferrum/net/reliable_channel.h"
#include "ferrum/net/topic_channel.h"

struct fr_rudp_stream {
    uint32_t reliable_channels;
    net_reliable_channel_t *reliable;
    fr_topic_channel_t **topics;
    uint32_t num_topics;
    size_t max_payload_size;
    uint8_t *scratch;
};

#endif /* FR_RUDP_STREAM_INTERNAL_H */