#include <string.h>

#include "ferrum/net/test_client.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/replication/common.h"

#include "test_client_internal.h"

bool fr_test_client_pump_rx(fr_test_client_t *cl, uint64_t now_ms) {
    if (!cl || !cl->rx_link) {
        return false;
    }

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    for (;;) {
        size_t packet_size = 0u;
        int rc = net_test_link_receive(cl->rx_link, packet, sizeof(packet), &packet_size);
        if (rc == NET_TEST_LINK_EMPTY) {
            break;
        }
        if (rc != NET_TEST_LINK_OK) {
            return false;
        }

        uint8_t reliable = 0u;
        uint16_t schema_id = 0u;
        uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
        size_t payload_size = 0u;

        int prc = net_rudp_peer_receive(&cl->peer,
                                        packet,
                                        packet_size,
                                        now_ms,
                                        &reliable,
                                        &schema_id,
                                        payload,
                                        sizeof(payload),
                                        &payload_size);
        if (prc != NET_RUDP_OK) {
            continue;
        }

        if (reliable && schema_id == NET_REPL_SCHEMA_STREAM_FRAME) {
            (void)fr_rudp_stream_push_frame(cl->stream, payload, payload_size);
            continue;
        }

        uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
        if (payload_size > NET_RUDP_MAX_PACKET_SIZE) {
            return false;
        }
        msg[0] = (uint8_t)(schema_id & 0xFFu);
        msg[1] = (uint8_t)((schema_id >> 8u) & 0xFFu);
        if (payload_size > 0u) {
            memcpy(msg + 2u, payload, payload_size);
        }

        if (!fr_topic_channel_push(cl->unreliable_inbox, msg, 2u + payload_size)) {
            return false;
        }
    }

    return true;
}

bool fr_test_client_pop_reliable(fr_test_client_t *cl,
                                uint32_t channel_id,
                                uint8_t *out,
                                size_t *inout_len) {
    if (!cl || !cl->stream) {
        return false;
    }
    return fr_rudp_stream_pop(cl->stream, channel_id, out, inout_len);
}

bool fr_test_client_pop_unreliable(fr_test_client_t *cl,
                                  uint16_t *out_schema_id,
                                  uint8_t *out,
                                  size_t *inout_len) {
    if (!cl || !cl->unreliable_inbox || !out_schema_id || !out || !inout_len) {
        return false;
    }

    uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
    size_t msg_len = sizeof(msg);
    if (!fr_topic_channel_pop(cl->unreliable_inbox, msg, &msg_len)) {
        return false;
    }
    if (msg_len < 2u) {
        return false;
    }

    uint16_t schema_id = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8u);
    size_t payload_size = msg_len - 2u;
    if (*inout_len < payload_size) {
        return false;
    }

    if (payload_size > 0u) {
        memcpy(out, msg + 2u, payload_size);
    }
    *out_schema_id = schema_id;
    *inout_len = payload_size;
    return true;
}
