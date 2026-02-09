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
    if (s->topics && chan < s->num_topics && s->topics[chan]) {
        for (;;) {
            size_t out_size = 0u;
            rc = net_reliable_channel_receive(ch, s->scratch, s->max_payload_size, &out_size);
            if (rc != NET_RELIABLE_OK) {
                break;
            }
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

bool fr_rudp_stream_send(fr_rudp_stream_t *s, uint32_t channel_id,
                         const uint8_t *payload, size_t payload_len) {
    if (!s || !s->outbound) {
        return false;
    }
    if (channel_id >= s->reliable_channels) {
        return false;
    }
    if (!payload && payload_len > 0u) {
        return false;
    }
    net_reliable_channel_t *ch = &s->outbound[channel_id];
    int rc = net_reliable_channel_send(ch, payload, payload_len);
    return rc == NET_RELIABLE_OK;
}

uint32_t fr_rudp_stream_flush_send(fr_rudp_stream_t *s,
                                   fr_rudp_stream_sendto_fn sendto,
                                   void *user) {
    if (!s || !s->outbound || !sendto || !s->frame_scratch) {
        return 0u;
    }

    uint32_t flushed = 0u;
    /* Frame header: [seq:u16 LE][chan:u16 LE] = 4 bytes before payload. */
    const size_t frame_hdr = 4u;

    for (uint32_t chan = 0u; chan < s->reliable_channels; ++chan) {
        net_reliable_channel_t *ch = &s->outbound[chan];
        for (;;) {
            size_t payload_size = 0u;
            /* Receive pops the next in-order message from the channel. */
            int rc = net_reliable_channel_receive(ch, s->scratch, s->max_payload_size, &payload_size);
            if (rc != NET_RELIABLE_OK) {
                break;
            }

            /* The sequence of the message we just popped is
             * (next_receive_sequence - 1) because receive() already
             * advanced next_receive_sequence. */
            uint16_t seq = (uint16_t)(ch->next_receive_sequence - 1u);

            /* Build frame: [seq_lo][seq_hi][chan_lo][chan_hi][payload] */
            s->frame_scratch[0] = (uint8_t)(seq & 0xFFu);
            s->frame_scratch[1] = (uint8_t)((seq >> 8u) & 0xFFu);
            s->frame_scratch[2] = (uint8_t)(chan & 0xFFu);
            s->frame_scratch[3] = (uint8_t)((chan >> 8u) & 0xFFu);
            if (payload_size > 0u) {
                memcpy(s->frame_scratch + frame_hdr, s->scratch, payload_size);
            }

            if (sendto(user, s->frame_scratch, frame_hdr + payload_size) != 0) {
                break; /* stop on send failure */
            }
            flushed++;
        }
    }
    return flushed;
}
