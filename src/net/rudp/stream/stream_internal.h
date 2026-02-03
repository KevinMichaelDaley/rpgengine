#ifndef FR_RUDP_STREAM_INTERNAL_H
#define FR_RUDP_STREAM_INTERNAL_H

#include <stdint.h>
#include "ferrum/net/reliable_channel.h"

struct fr_rudp_stream {
    uint32_t reliable_channels;
    net_reliable_channel_t *reliable;
};

#endif /* FR_RUDP_STREAM_INTERNAL_H */