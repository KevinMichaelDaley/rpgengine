#include <string.h>

#include "ferrum/net/stream.h"
#include "ferrum/net/reliable_channel.h"
#include "src/net/rudp/stream/stream_internal.h"

bool fr_rudp_stream_push_frame(fr_rudp_stream_t *s, const uint8_t *data, size_t len) {
    if (!s || !data || len < 2u) {
        return false;
    }
    /* Minimal frame format: [seq_lo][seq_hi][payload...] */
    uint16_t seq = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    const uint8_t *payload = data + 2u;
    size_t payload_size = len - 2u;
    /* Channel 0 for initial implementation. */
    net_reliable_channel_t *ch = &s->reliable[0];
    int rc = net_reliable_channel_send_sequence(ch, seq, payload, payload_size);
    return rc == NET_RELIABLE_OK;
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
