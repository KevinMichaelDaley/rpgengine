#include <stdlib.h>
#include <string.h>

#include "ferrum/net/stream.h"
#include "ferrum/net/reliable_channel.h"
#include "src/net/rudp/stream/stream_internal.h"

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
    if (!s->reliable) {
        free(s);
        return NULL;
    }
    for (uint32_t i = 0u; i < channels; ++i) {
        net_reliable_channel_init(&s->reliable[i], (size_t)slot_count, (size_t)max_payload);
        if (!s->reliable[i].initialized) {
            fr_rudp_stream_destroy(s);
            return NULL;
        }
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
    free(s);
}
