/**
 * @file server_stream_priority.c
 * @brief Server-side streaming-priority computation from player interest (rpg-3ldk).
 */
#include "ferrum/server/server_stream_priority.h"
#include "ferrum/net/replication/stream_priority.h"

/* Squared distance from point p to AABB [lo,hi] (0 if inside). */
static float box_dist2(const float p[3], const float lo[3], const float hi[3])
{
    float d2 = 0.0f;
    for (int a = 0; a < 3; ++a) {
        float v = p[a];
        if (v < lo[a]) { float e = lo[a] - v; d2 += e * e; }
        else if (v > hi[a]) { float e = v - hi[a]; d2 += e * e; }
    }
    return d2;
}

uint32_t server_stream_priority_build(const uint64_t *ids, const float *box_min,
                                      const float *box_max, uint32_t n,
                                      const float player[3], float scale,
                                      struct net_repl_stream_priority *outp)
{
    if (ids == NULL || box_min == NULL || box_max == NULL || player == NULL ||
        outp == NULL)
        return 0u;
    net_repl_stream_priority_t *out = outp;
    uint32_t cnt = (n < NET_REPL_STREAM_PRIORITY_MAX) ? n
                                                      : NET_REPL_STREAM_PRIORITY_MAX;
    for (uint32_t i = 0; i < cnt; ++i) {
        float d2 = box_dist2(player, &box_min[i * 3], &box_max[i * 3]);
        out->entries[i].id = ids[i];
        out->entries[i].priority = -(int32_t)(d2 * scale); /* nearer => higher. */
    }
    out->count = (uint16_t)cnt;
    return cnt;
}
