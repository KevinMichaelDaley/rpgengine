#include <stdlib.h>
#include <string.h>

#include "ferrum/net/stream.h"
#include "ferrum/net/reliable_channel.h"
#include "stream_internal.h"

static uint32_t clamp_default_u32(uint32_t v, uint32_t def) {
    return v == 0u ? def : v;
}

fr_rudp_stream_t *fr_rudp_stream_create(const fr_rudp_stream_config_t *cfg) {
    uint32_t channels = 1u;
    uint32_t slot_count = 64u;
    uint32_t max_payload = 1024u;
    if (cfg) {
        channels = clamp_default_u32(cfg->reliable_channels, 1u);
        slot_count = clamp_default_u32(cfg->reliable_slot_count, 64u);
        max_payload = clamp_default_u32(cfg->max_payload_size, 1024u);
    }

    fr_rudp_stream_t *s = (fr_rudp_stream_t *)calloc(1, sizeof(fr_rudp_stream_t));
    if (!s) {
        return NULL;
    }
    s->reliable_channels = channels;
    s->reliable = (net_reliable_channel_t *)calloc(channels, sizeof(net_reliable_channel_t));
    s->outbound = (net_reliable_channel_t *)calloc(channels, sizeof(net_reliable_channel_t));
    if (!s->reliable || !s->outbound) {
        free(s->reliable);
        free(s->outbound);
        free(s);
        return NULL;
    }
    s->topics = NULL;
    s->num_topics = 0u;
    s->max_payload_size = max_payload;
    s->scratch = (uint8_t *)malloc((size_t)max_payload);
    s->frame_scratch = (uint8_t *)malloc(4u + (size_t)max_payload);
    if (!s->scratch || !s->frame_scratch) {
        free(s->scratch);
        free(s->frame_scratch);
        free(s->reliable);
        free(s->outbound);
        free(s);
        return NULL;
    }
    if (cfg && cfg->topics && cfg->num_topics > 0u) {
        s->topics = cfg->topics;
        s->num_topics = cfg->num_topics;
    }
    for (uint32_t i = 0u; i < channels; ++i) {
        net_reliable_channel_init(&s->reliable[i], (size_t)slot_count, (size_t)max_payload);
        if (!s->reliable[i].initialized) {
            fr_rudp_stream_destroy(s);
            return NULL;
        }
        /* Client RX tests expect sequences starting at 1. */
        s->reliable[i].next_receive_sequence = 1u;

        net_reliable_channel_init(&s->outbound[i], (size_t)slot_count, (size_t)max_payload);
        if (!s->outbound[i].initialized) {
            fr_rudp_stream_destroy(s);
            return NULL;
        }
        /* Outbound sequences start at 1 to match receiver expectations. */
        s->outbound[i].next_send_sequence = 1u;
        s->outbound[i].next_receive_sequence = 1u;
    }
    return s;
}

void fr_rudp_stream_destroy(fr_rudp_stream_t *s) {
    if (!s) {
        return;
    }
    if (s->reliable) {
        for (uint32_t i = 0u; i < s->reliable_channels; ++i) {
            net_reliable_channel_destroy(&s->reliable[i]);
        }
        free(s->reliable);
        s->reliable = NULL;
    }
    if (s->outbound) {
        for (uint32_t i = 0u; i < s->reliable_channels; ++i) {
            net_reliable_channel_destroy(&s->outbound[i]);
        }
        free(s->outbound);
        s->outbound = NULL;
    }
    free(s->scratch);
    s->scratch = NULL;
    free(s->frame_scratch);
    s->frame_scratch = NULL;
    free(s);
}
