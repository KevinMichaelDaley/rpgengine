/**
 * @file snapshot_delta.c
 * @brief Snapshot delta compute and apply.
 *
 * Non-static functions: 2 (compute, apply).
 */

#include "ferrum/net/snapshot_delta.h"
#include <string.h>

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
 * Compare two bodies and produce a changed_mask bitmask.
 */
static uint8_t compare_bodies(const net_snap_body_t *base,
                               const net_snap_body_t *cur) {
    uint8_t mask = 0;
    if (memcmp(base->position, cur->position, sizeof(base->position)) != 0) {
        mask |= NET_SNAP_CHANGED_POS;
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
        if (!base_body) {
            /* New body — all fields changed. */
            mask = NET_SNAP_CHANGED_ALL;
        } else {
            mask = compare_bodies(base_body, cur_body);
            if (mask == 0) { continue; } /* No change. */
        }

        if (delta->count >= delta->capacity) { return NET_SNAP_FULL; }

        net_snap_delta_entry_t *e = &delta->entries[delta->count++];
        e->body_id = cur_body->body_id;
        e->changed_mask = mask;
        e->data = *cur_body;
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
             * caller allocated enough space in bodies[]. */
            body = &snapshot->bodies[snapshot->body_count++];
            *body = e->data;
            continue;
        }

        /* Merge changed fields. */
        if (e->changed_mask & NET_SNAP_CHANGED_POS) {
            memcpy(body->position, e->data.position, sizeof(body->position));
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
