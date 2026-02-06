/**
 * @file snapshot_decode.c
 * @brief Decode binary snapshot into phys_snapshot_t and apply to world.
 *
 * Reads the wire format produced by phys_snapshot_encode() and
 * optionally applies the decoded state to a live physics world.
 */

#include "ferrum/physics/snapshot.h"
#include "ferrum/physics/world.h"

#include <string.h>

/* ── Constants ──────────────────────────────────────────────────── */

/** Header size: 8 bytes tick + 4 bytes body_count. */
#define SNAPSHOT_HEADER_SIZE 12

/** Quantized body size in bytes. */
#define SNAPSHOT_BODY_SIZE 26

/** Position and velocity inverse scale (1 / 1000). */
#define POS_VEL_INV_SCALE (1.0f / 1000.0f)

/* ── Public API ─────────────────────────────────────────────────── */

int phys_snapshot_decode(const uint8_t *buffer, size_t size,
                         phys_snapshot_t *snapshot_out)
{
    if (!buffer || !snapshot_out) return -1;
    if (size < SNAPSHOT_HEADER_SIZE) return -1;

    /* Read header. */
    uint64_t tick;
    uint32_t body_count;
    memcpy(&tick, buffer, 8);
    memcpy(&body_count, buffer + 8, 4);

    /* Validate buffer has enough data for all bodies. */
    size_t required = SNAPSHOT_HEADER_SIZE + (size_t)body_count * SNAPSHOT_BODY_SIZE;
    if (size < required) return -1;

    snapshot_out->tick = tick;
    snapshot_out->body_count = body_count;

    /* Read each body. */
    const uint8_t *src = buffer + SNAPSHOT_HEADER_SIZE;
    for (uint32_t i = 0; i < body_count; i++) {
        phys_snapshot_body_t *snap = &snapshot_out->bodies[i];
        memcpy(snap->position,    src +  0, 6);
        memcpy(snap->orientation, src +  6, 6);
        memcpy(snap->linear_vel,  src + 12, 6);
        memcpy(snap->angular_vel, src + 18, 6);
        snap->flags = src[24];
        snap->tier  = src[25];
        src += SNAPSHOT_BODY_SIZE;
    }

    return 0;
}

int phys_snapshot_apply(struct phys_world *world,
                        const phys_snapshot_t *snapshot)
{
    if (!world || !snapshot) return -1;

    phys_body_pool_t *pool = &world->body_pool;
    world->tick_count = snapshot->tick;

    /* Apply each snapshot body to the corresponding active slot. */
    uint32_t snap_idx = 0;
    for (uint32_t i = 0; i < pool->capacity && snap_idx < snapshot->body_count; i++) {
        if (!pool->active[i]) continue;

        const phys_snapshot_body_t *snap = &snapshot->bodies[snap_idx];
        phys_body_t *body = &pool->bodies_curr[i];

        body->position   = phys_dequantize_vec3(snap->position, POS_VEL_INV_SCALE);
        body->orientation = phys_dequantize_quat(snap->orientation);
        body->linear_vel  = phys_dequantize_vec3(snap->linear_vel, POS_VEL_INV_SCALE);
        body->angular_vel = phys_dequantize_vec3(snap->angular_vel, POS_VEL_INV_SCALE);
        body->flags = (uint32_t)snap->flags;
        body->tier  = snap->tier;

        snap_idx++;
    }

    return 0;
}
