/**
 * @file snapshot_encode.c
 * @brief Encode physics world state into a compact binary snapshot.
 *
 * Wire format: [tick:8 LE][body_count:4 LE][body_0:26][body_1:26]...
 * Each body is 26 bytes of quantized state.
 */

#include "ferrum/physics/snapshot.h"
#include "ferrum/physics/world.h"

#include <string.h>

/* ── Constants ──────────────────────────────────────────────────── */

/** Header size: 8 bytes tick + 4 bytes body_count. */
#define SNAPSHOT_HEADER_SIZE 12

/** Quantized body size in bytes. */
#define SNAPSHOT_BODY_SIZE 26

/** Position and velocity quantization scale (millimeters). */
#define POS_VEL_SCALE 1000.0f

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Write one quantized body into the buffer at the given offset.
 *
 * @param body   Source body (non-NULL).
 * @param buffer Destination buffer at body offset (non-NULL).
 */
static void write_body(const phys_body_t *body, uint8_t *buffer)
{
    phys_snapshot_body_t snap;
    phys_quantize_vec3(body->position, snap.position, POS_VEL_SCALE);
    phys_quantize_quat(body->orientation, snap.orientation);
    phys_quantize_vec3(body->linear_vel, snap.linear_vel, POS_VEL_SCALE);
    phys_quantize_vec3(body->angular_vel, snap.angular_vel, POS_VEL_SCALE);
    snap.flags = (uint8_t)(body->flags & 0xFF);
    snap.tier = body->tier;

    /* Write raw bytes — no padding assumed; copy field by field. */
    memcpy(buffer +  0, snap.position,    6);
    memcpy(buffer +  6, snap.orientation,  6);
    memcpy(buffer + 12, snap.linear_vel,   6);
    memcpy(buffer + 18, snap.angular_vel,  6);
    buffer[24] = snap.flags;
    buffer[25] = snap.tier;
}

/* ── Public API ─────────────────────────────────────────────────── */

size_t phys_snapshot_encode(const struct phys_world *world,
                            uint8_t *buffer, size_t max_size)
{
    if (!world || !buffer) return 0;

    const phys_body_pool_t *pool = &world->body_pool;
    uint32_t active_count = phys_body_pool_active_count(pool);

    size_t required = SNAPSHOT_HEADER_SIZE + (size_t)active_count * SNAPSHOT_BODY_SIZE;
    if (max_size < required) return 0;

    /* Write header: tick (8 bytes LE) + body_count (4 bytes LE). */
    uint64_t tick = world->tick_count;
    memcpy(buffer, &tick, 8);
    memcpy(buffer + 8, &active_count, 4);

    /* Write each active body. */
    size_t offset = SNAPSHOT_HEADER_SIZE;
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (!pool->active[i]) continue;
        write_body(&pool->bodies_curr[i], buffer + offset);
        offset += SNAPSHOT_BODY_SIZE;
    }

    return offset;
}
