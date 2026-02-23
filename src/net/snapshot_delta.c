/**
 * @file snapshot_delta.c
 * @brief Snapshot delta compute and apply.
 *
 * Position deltas are encoded in float-domain: the difference between
 * current and baseline float positions is quantized at high precision
 * (NET_SNAP_DELTA_SCALE = 4096, ~0.24mm).  If the delta overflows
 * int16 range, the entry falls back to absolute encoding.
 *
 * Non-static functions: 2 (compute, apply).
 */

#include "ferrum/net/snapshot_delta.h"
#include <stdbool.h>
#include <string.h>
#include <math.h>

/* ── Helpers (static) ──────────────────────────────────────────── */

/**
 * Find a body by body_id in a snapshot.
 * Returns pointer to the body, or NULL if not found.
 */
static const net_snap_body_t *find_body(const net_snapshot_t *snap,
                                        uint16_t body_id) {
    for (uint32_t i = 0; i < snap->body_count; i++) {
        if (snap->bodies[i].body_id == body_id) {
            return &snap->bodies[i];
        }
    }
    return NULL;
}

/**
 * Dequantize a single int16 position component at the given scale.
 */
static float deq_(int16_t v, float inv_scale) {
    return (float)v * inv_scale;
}

/**
 * Quantize a float to int16, clamping to [-32767, 32767].
 * Returns false if the value overflows the range.
 */
static bool quant_checked_(float v, float scale, int16_t *out) {
    float s = v * scale;
    if (s < -32767.0f || s > 32767.0f) { return false; }
    *out = (int16_t)roundf(s);
    return true;
}

/**
 * Compare two bodies and produce a changed_mask bitmask.
 * For position: computes a float-domain delta from baseline to current,
 * quantized at NET_SNAP_DELTA_SCALE.  If the delta fits in int16,
 * sets NET_SNAP_DELTA_POS | NET_SNAP_CHANGED_POS and writes delta
 * values into delta_pos_out.  If it overflows, sets only
 * NET_SNAP_CHANGED_POS (absolute fallback).
 */
static uint8_t compare_bodies_delta(const net_snap_body_t *base,
                                     const net_snap_body_t *cur,
                                     int16_t delta_pos_out[3]) {
    uint8_t mask = 0;

    /* Position: float-domain delta at high precision. */
    const float abs_inv = 1.0f / NET_SNAP_ABS_SCALE;
    float base_f[3], cur_f[3];
    for (int k = 0; k < 3; k++) {
        base_f[k] = deq_(base->position[k], abs_inv);
        cur_f[k]  = deq_(cur->position[k],  abs_inv);
    }

    bool delta_ok = true;
    int16_t dp[3];
    for (int k = 0; k < 3; k++) {
        float diff = cur_f[k] - base_f[k];
        if (!quant_checked_(diff, NET_SNAP_DELTA_SCALE, &dp[k])) {
            delta_ok = false;
            break;
        }
    }

    /* Check if position actually changed (any delta != 0). */
    if (delta_ok) {
        if (dp[0] != 0 || dp[1] != 0 || dp[2] != 0) {
            mask |= NET_SNAP_CHANGED_POS | NET_SNAP_DELTA_POS;
            delta_pos_out[0] = dp[0];
            delta_pos_out[1] = dp[1];
            delta_pos_out[2] = dp[2];
        }
    } else {
        /* Delta overflowed int16 — check if absolute values differ. */
        if (memcmp(base->position, cur->position, sizeof(base->position)) != 0) {
            mask |= NET_SNAP_CHANGED_POS;
            /* delta_pos_out not used; apply reads data.position directly. */
        }
    }

    if (memcmp(base->orientation, cur->orientation, sizeof(base->orientation)) != 0) {
        mask |= NET_SNAP_CHANGED_ORI;
    }
    if (memcmp(base->linear_vel, cur->linear_vel, sizeof(base->linear_vel)) != 0) {
        mask |= NET_SNAP_CHANGED_LINVEL;
    }
    if (memcmp(base->angular_vel, cur->angular_vel, sizeof(base->angular_vel)) != 0) {
        mask |= NET_SNAP_CHANGED_ANGVEL;
    }
    if (base->flags != cur->flags || base->tier != cur->tier) {
        mask |= NET_SNAP_CHANGED_FLAGS;
    }
    return mask;
}

/* ── Public ────────────────────────────────────────────────────── */

int net_snapshot_delta_compute(const net_snapshot_t *baseline,
                               const net_snapshot_t *current,
                               net_snapshot_delta_t *delta) {
    if (!baseline || !current || !delta || !delta->entries) {
        return NET_SNAP_ERR_INVALID;
    }

    delta->count = 0;
    delta->base_tick = baseline->tick;
    delta->cur_tick = current->tick;

    /* Bodies in current: compare against baseline or mark as new. */
    for (uint32_t i = 0; i < current->body_count; i++) {
        const net_snap_body_t *cur_body = &current->bodies[i];
        const net_snap_body_t *base_body = find_body(baseline, cur_body->body_id);

        uint8_t mask;
        int16_t delta_pos[3] = {0, 0, 0};
        if (!base_body) {
            /* New body — all fields changed, absolute position. */
            mask = NET_SNAP_CHANGED_ALL;
        } else {
            mask = compare_bodies_delta(base_body, cur_body, delta_pos);
            if (mask == 0) { continue; } /* No change. */
        }

        if (delta->count >= delta->capacity) { return NET_SNAP_FULL; }

        net_snap_delta_entry_t *e = &delta->entries[delta->count++];
        e->body_id = cur_body->body_id;
        e->changed_mask = mask;
        e->data = *cur_body;

        /* If delta-encoded, overwrite position with delta values. */
        if (mask & NET_SNAP_DELTA_POS) {
            e->data.position[0] = delta_pos[0];
            e->data.position[1] = delta_pos[1];
            e->data.position[2] = delta_pos[2];
        }
    }

    /* Bodies in baseline but not in current → destroyed. */
    for (uint32_t i = 0; i < baseline->body_count; i++) {
        const net_snap_body_t *base_body = &baseline->bodies[i];
        if (!find_body(current, base_body->body_id)) {
            if (delta->count >= delta->capacity) { return NET_SNAP_FULL; }

            net_snap_delta_entry_t *e = &delta->entries[delta->count++];
            e->body_id = base_body->body_id;
            e->changed_mask = NET_SNAP_CHANGED_DESTROY;
            memset(&e->data, 0, sizeof(e->data));
        }
    }

    return NET_SNAP_OK;
}

int net_snapshot_delta_apply(net_snapshot_t *snapshot,
                             const net_snapshot_delta_t *delta) {
    if (!snapshot || !delta || !delta->entries) {
        return NET_SNAP_ERR_INVALID;
    }

    for (uint32_t i = 0; i < delta->count; i++) {
        const net_snap_delta_entry_t *e = &delta->entries[i];

        /* Skip destroy entries — caller handles body removal. */
        if (e->changed_mask & NET_SNAP_CHANGED_DESTROY) { continue; }

        /* Find the body in the snapshot to update. */
        net_snap_body_t *body = NULL;
        for (uint32_t j = 0; j < snapshot->body_count; j++) {
            if (snapshot->bodies[j].body_id == e->body_id) {
                body = &snapshot->bodies[j];
                break;
            }
        }

        if (!body) {
            /* New body — append if there's room. We assume the
             * caller allocated enough space in bodies[].
             * New bodies always use absolute positions. */
            body = &snapshot->bodies[snapshot->body_count++];
            *body = e->data;
            continue;
        }

        /* Merge changed fields. */
        if (e->changed_mask & NET_SNAP_CHANGED_POS) {
            if (e->changed_mask & NET_SNAP_DELTA_POS) {
                /* Delta-encoded: reconstruct position from baseline
                 * (currently in body->position) + delta. */
                const float abs_inv   = 1.0f / NET_SNAP_ABS_SCALE;
                const float delta_inv = 1.0f / NET_SNAP_DELTA_SCALE;
                for (int k = 0; k < 3; k++) {
                    float base_f  = deq_(body->position[k], abs_inv);
                    float delta_f = deq_(e->data.position[k], delta_inv);
                    float result  = base_f + delta_f;
                    /* Re-quantize to absolute scale for storage. */
                    float s = result * NET_SNAP_ABS_SCALE;
                    if (s < -32767.0f) s = -32767.0f;
                    if (s >  32767.0f) s =  32767.0f;
                    body->position[k] = (int16_t)roundf(s);
                }
            } else {
                /* Absolute fallback. */
                memcpy(body->position, e->data.position,
                       sizeof(body->position));
            }
        }
        if (e->changed_mask & NET_SNAP_CHANGED_ORI) {
            memcpy(body->orientation, e->data.orientation,
                   sizeof(body->orientation));
        }
        if (e->changed_mask & NET_SNAP_CHANGED_LINVEL) {
            memcpy(body->linear_vel, e->data.linear_vel,
                   sizeof(body->linear_vel));
        }
        if (e->changed_mask & NET_SNAP_CHANGED_ANGVEL) {
            memcpy(body->angular_vel, e->data.angular_vel,
                   sizeof(body->angular_vel));
        }
        if (e->changed_mask & NET_SNAP_CHANGED_FLAGS) {
            body->flags = e->data.flags;
            body->tier = e->data.tier;
        }
    }

    snapshot->tick = delta->cur_tick;
    return NET_SNAP_OK;
}
