/**
 * @file amortized_interp.c
 * @brief Amortized ticking — snapshot and interpolation for T4 bodies.
 *
 * T4 (background/distant) bodies tick every 3rd frame.  This module
 * stores per-body snapshots on tick frames and linearly interpolates
 * (lerp/slerp) visual poses on intermediate frames.
 */

#include "ferrum/physics/amortized.h"
#include "ferrum/physics/tier_list.h"

#include <stdlib.h>
#include <string.h>

/* ── Quaternion epsilon for slerp normalisation ─────────────────── */
#define QUAT_EPSILON 1e-6f

/* ── Init / Destroy ─────────────────────────────────────────────── */

bool phys_amortized_init(phys_amortized_state_t *state,
                         uint32_t body_capacity)
{
    if (!state) {
        return false;
    }

    memset(state, 0, sizeof(*state));

    state->prev_positions = calloc(body_capacity, sizeof(phys_vec3_t));
    if (!state->prev_positions) {
        return false;
    }

    state->prev_orientations = calloc(body_capacity, sizeof(phys_quat_t));
    if (!state->prev_orientations) {
        free(state->prev_positions);
        state->prev_positions = NULL;
        return false;
    }

    state->body_capacity   = body_capacity;
    state->last_tick_frame = 0;
    return true;
}

void phys_amortized_destroy(phys_amortized_state_t *state)
{
    if (!state) {
        return;
    }

    free(state->prev_positions);
    free(state->prev_orientations);
    memset(state, 0, sizeof(*state));
}

/* ── Snapshot ───────────────────────────────────────────────────── */

void phys_amortized_snapshot(phys_amortized_state_t *state,
                             const phys_body_t *bodies,
                             uint32_t body_count,
                             uint32_t current_frame)
{
    if (!state || !bodies) {
        return;
    }

    /* Only snapshot on T4 tick frames. */
    if (current_frame % PHYS_T4_TICK_INTERVAL != 0) {
        return;
    }

    uint32_t count = body_count < state->body_capacity
                         ? body_count
                         : state->body_capacity;

    for (uint32_t i = 0; i < count; i++) {
        if (bodies[i].tier == PHYS_TIER_4_BACKGROUND) {
            state->prev_positions[i]    = bodies[i].position;
            state->prev_orientations[i] = bodies[i].orientation;
        }
    }

    state->last_tick_frame = current_frame;
}

/* ── Interpolation ──────────────────────────────────────────────── */

void phys_amortized_interpolate(const phys_amortized_state_t *state,
                                const phys_body_t *bodies,
                                uint32_t body_count,
                                uint32_t current_frame,
                                phys_vec3_t *visual_pos,
                                phys_quat_t *visual_rot)
{
    if (!state || !bodies || !visual_pos || !visual_rot) {
        return;
    }

    /* Compute interpolation alpha: 0 on tick frame, up to ~1 on frame before next tick. */
    float alpha = (float)(current_frame - state->last_tick_frame)
                  / (float)PHYS_T4_TICK_INTERVAL;

    /* Clamp alpha to [0, 1]. */
    if (alpha < 0.0f) { alpha = 0.0f; }
    if (alpha > 1.0f) { alpha = 1.0f; }

    uint32_t count = body_count < state->body_capacity
                         ? body_count
                         : state->body_capacity;

    for (uint32_t i = 0; i < count; i++) {
        if (bodies[i].tier == PHYS_TIER_4_BACKGROUND) {
            /* Interpolate position: lerp(prev, current, alpha). */
            visual_pos[i] = vec3_lerp(state->prev_positions[i],
                                      bodies[i].position, alpha);

            /* Interpolate orientation: slerp(prev, current, alpha). */
            visual_rot[i] = quat_slerp(state->prev_orientations[i],
                                       bodies[i].orientation,
                                       alpha, QUAT_EPSILON);
        } else {
            /* Non-T4 bodies: copy current pose verbatim. */
            visual_pos[i] = bodies[i].position;
            visual_rot[i] = bodies[i].orientation;
        }
    }
}
