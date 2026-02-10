#include <stdlib.h>
#include <string.h>

#include "ferrum/net/test_client.h"
#include "ferrum/net/topic_channel.h"

#include "test_client_internal.h"

fr_test_client_t *fr_test_client_create(const fr_test_client_config_t *cfg) {
    if (!cfg || !cfg->tx_link || !cfg->rx_link) {
        return NULL;
    }

    fr_test_client_t *cl = (fr_test_client_t *)calloc(1u, sizeof(*cl));
    if (!cl) {
        return NULL;
    }

    cl->tx_link = cfg->tx_link;
    cl->rx_link = cfg->rx_link;
    cl->remote_addr = cfg->remote_addr;

    uint32_t protocol_id = cfg->protocol_id;
    if (protocol_id == 0u) {
        protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    }

    net_rudp_peer_init_with_storage(&cl->peer,
                                   protocol_id,
                                   cfg->resend_interval_ms,
                                   cl->send_slots,
                                   (size_t)(sizeof(cl->send_slots) / sizeof(cl->send_slots[0])));

    fr_rudp_stream_config_t scfg;
    memset(&scfg, 0, sizeof(scfg));
    scfg.reliable_channels = (cfg->stream_channels != 0u) ? cfg->stream_channels : 1u;
    scfg.reliable_slot_count = (cfg->stream_slots != 0u) ? cfg->stream_slots : 64u;
    scfg.max_payload_size = (cfg->stream_max_payload != 0u) ? cfg->stream_max_payload : NET_RUDP_MAX_PACKET_SIZE;

    cl->stream = fr_rudp_stream_create(&scfg);
    if (!cl->stream) {
        free(cl);
        return NULL;
    }

    fr_topic_channel_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.capacity = (cfg->unreliable_inbox_capacity != 0u) ? cfg->unreliable_inbox_capacity : 64u;
    cl->unreliable_inbox = fr_topic_channel_create(&tcfg);
    if (!cl->unreliable_inbox) {
        fr_rudp_stream_destroy(cl->stream);
        free(cl);
        return NULL;
    }

    return cl;
}

void fr_test_client_destroy(fr_test_client_t *cl) {
    if (!cl) {
        return;
    }

    if (cl->stream) {
        fr_rudp_stream_destroy(cl->stream);
        cl->stream = NULL;
    }

    if (cl->unreliable_inbox) {
        fr_topic_channel_destroy(cl->unreliable_inbox);
        cl->unreliable_inbox = NULL;
    }

    free(cl);
}
