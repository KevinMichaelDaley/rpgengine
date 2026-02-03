#include <string.h>

#include "ferrum/net/stream.h"
#include "ferrum/net/reliable_channel.h"
#include "stream_internal.h"

bool fr_rudp_stream_push_frame(fr_rudp_stream_t *s, const uint8_t *data, size_t len) {
    if (!s || !data || len < 4u) {
        return false;
    }
    /* Frame format: [seq_lo][seq_hi][chan_lo][chan_hi][payload...] */
    uint16_t seq = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    uint16_t chan = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
    if (chan >= s->reliable_channels) {
        return false;
    }
    const uint8_t *payload = data + 4u;
    size_t payload_size = len - 4u;
    net_reliable_channel_t *ch = &s->reliable[chan];
    int rc = net_reliable_channel_send_sequence(ch, seq, payload, payload_size);
    if (rc != NET_RELIABLE_OK) {
        return false;
    }
    /* Pump any newly in-order messages to topic channels if configured. */
    for (;;) {
        size_t out_size = 0u;
        rc = net_reliable_channel_receive(ch, s->scratch, s->max_payload_size, &out_size);
        if (rc != NET_RELIABLE_OK) {
            break;
        }
        if (s->topics && chan < s->num_topics && s->topics[chan]) {
            (void)fr_topic_channel_push(s->topics[chan], s->scratch, out_size);
        }
    }
    return true;
}

bool fr_rudp_stream_pop(fr_rudp_stream_t *s, uint32_t channel_id, uint8_t *out, size_t *inout_len) {
    if (!s || !out || !inout_len) {
        return false;
    }
    if (channel_id >= s->reliable_channels) {
        return false;
    }
    net_reliable_channel_t *ch = &s->reliable[channel_id];
    size_t out_size = 0u;
    int rc = net_reliable_channel_receive(ch, out, *inout_len, &out_size);
    if (rc != NET_RELIABLE_OK) {
        return false;
    }
    *inout_len = out_size;
    return true;
}
