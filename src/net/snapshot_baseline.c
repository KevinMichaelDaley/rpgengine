/**
 * @file snapshot_baseline.c
 * @brief Per-client baseline tracker with snapshot history ring.
 *
 * Non-static functions: 3 (init, record, ack).
 */

#include "ferrum/net/snapshot_delta.h"
#include <string.h>

void net_snap_baseline_init(net_snap_baseline_t *bl,
                            net_snap_body_t *baseline_bodies,
                            uint32_t max_bodies,
                            net_snapshot_t *ring,
                            net_snap_body_t *ring_bodies,
                            uint32_t bodies_per_slot,
                            uint32_t ring_cap) {
    if (!bl) { return; }

    memset(bl, 0, sizeof(*bl));

    /* Wire up baseline snapshot storage. */
    bl->baseline.bodies = baseline_bodies;
    bl->baseline.body_count = 0;
    bl->baseline.tick = 0;
    bl->baseline_tick = 0;

    /* Wire up ring buffer. */
    bl->ring = ring;
    bl->ring_bodies = ring_bodies;
    bl->bodies_per_slot = bodies_per_slot;
    bl->ring_capacity = ring_cap;
    bl->ring_write = 0;
    bl->ring_count = 0;

    /* Zero all ring slot bodies pointers. */
    if (ring && ring_bodies && ring_cap > 0) {
        for (uint32_t i = 0; i < ring_cap; i++) {
            ring[i].bodies = &ring_bodies[i * bodies_per_slot];
            ring[i].body_count = 0;
            ring[i].tick = 0;
        }
    }

    if (baseline_bodies && max_bodies > 0) {
        memset(baseline_bodies, 0, max_bodies * sizeof(net_snap_body_t));
    }
}

int net_snap_baseline_record(net_snap_baseline_t *bl,
                             const net_snapshot_t *snap) {
    if (!bl || !snap || !bl->ring) { return NET_SNAP_ERR_INVALID; }

    /* Write into the next ring slot, wrapping around. */
    uint32_t slot = bl->ring_write % bl->ring_capacity;
    net_snapshot_t *dest = &bl->ring[slot];

    dest->tick = snap->tick;
    /* Copy body data, clamped to bodies_per_slot. */
    uint32_t n = snap->body_count;
    if (n > bl->bodies_per_slot) { n = bl->bodies_per_slot; }
    dest->body_count = n;
    if (n > 0) {
        memcpy(dest->bodies, snap->bodies, n * sizeof(net_snap_body_t));
    }

    bl->ring_write++;
    if (bl->ring_count < bl->ring_capacity) {
        bl->ring_count++;
    }

    return NET_SNAP_OK;
}

int net_snap_baseline_ack(net_snap_baseline_t *bl, uint64_t tick) {
    if (!bl || !bl->ring) { return NET_SNAP_ERR_INVALID; }

    /* Search the ring for this tick. */
    for (uint32_t i = 0; i < bl->ring_count; i++) {
        /* Walk backwards from the most recent write. */
        uint32_t idx = (bl->ring_write - 1 - i) % bl->ring_capacity;
        if (bl->ring[idx].tick == tick) {
            /* Found — copy into baseline. */
            const net_snapshot_t *src = &bl->ring[idx];
            bl->baseline.tick = src->tick;
            uint32_t n = src->body_count;
            bl->baseline.body_count = n;
            if (n > 0) {
                memcpy(bl->baseline.bodies, src->bodies,
                       n * sizeof(net_snap_body_t));
            }
            bl->baseline_tick = tick;
            return NET_SNAP_OK;
        }
    }

    return NET_SNAP_BASELINE_EXPIRED;
}
