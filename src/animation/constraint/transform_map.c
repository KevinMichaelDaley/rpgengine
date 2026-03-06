/**
 * @file transform_map.c
 * @brief Transformation mapping constraint evaluator.
 *
 * Maps one transform channel of a target to another channel of the owner.
 * Supports clamped and extrapolated modes.
 *
 * Non-static functions: 1 (eval_transformation)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/**
 * @brief Read a transform channel value from a mat4.
 *
 * Location channels read from m[12..14].
 * Rotation channels extract Euler angles from the upper 3x3.
 * Scale channels extract column magnitudes.
 */
static float read_channel_(const mat4_t *m, constraint_channel_t ch) {
    switch (ch) {
        case CONSTRAINT_CHANNEL_LOC_X: return m->m[12];
        case CONSTRAINT_CHANNEL_LOC_Y: return m->m[13];
        case CONSTRAINT_CHANNEL_LOC_Z: return m->m[14];
        case CONSTRAINT_CHANNEL_SCL_X:
            return sqrtf(m->m[0]*m->m[0] + m->m[1]*m->m[1] + m->m[2]*m->m[2]);
        case CONSTRAINT_CHANNEL_SCL_Y:
            return sqrtf(m->m[4]*m->m[4] + m->m[5]*m->m[5] + m->m[6]*m->m[6]);
        case CONSTRAINT_CHANNEL_SCL_Z:
            return sqrtf(m->m[8]*m->m[8] + m->m[9]*m->m[9] + m->m[10]*m->m[10]);
        case CONSTRAINT_CHANNEL_ROT_X: {
            float c2 = sqrtf(m->m[10]*m->m[10] + m->m[6]*m->m[6]);
            if (c2 < 1e-7f) c2 = 1e-7f;
            return atan2f(m->m[6], m->m[10]);
        }
        case CONSTRAINT_CHANNEL_ROT_Y:
            return asinf(-m->m[2]);
        case CONSTRAINT_CHANNEL_ROT_Z:
            return atan2f(m->m[1], m->m[0]);
        default: return 0.0f;
    }
}

/**
 * @brief Write a value to a transform channel of a mat4.
 */
static void write_channel_(mat4_t *m, constraint_channel_t ch, float value) {
    switch (ch) {
        case CONSTRAINT_CHANNEL_LOC_X: m->m[12] = value; break;
        case CONSTRAINT_CHANNEL_LOC_Y: m->m[13] = value; break;
        case CONSTRAINT_CHANNEL_LOC_Z: m->m[14] = value; break;
        default: break; /* Rotation/scale writes would require decompose/recompose. */
    }
}

/**
 * @brief Transformation constraint evaluator.
 */
void eval_transformation(const constraint_def_t *def,
                         const constraint_eval_ctx_t *ctx,
                         mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    const constraint_transformation_params_t *p = &def->params.transformation;
    float range = p->from_max - p->from_min;
    if (fabsf(range) < 1e-7f) return; /* degenerate range */

    float source = read_channel_(&ctx->pose[def->target_bone_idx], p->from_channel);

    /* Compute t: normalized position within source range. */
    float t = (source - p->from_min) / range;

    /* Clamp if not extrapolating. */
    if (!p->extrapolate) {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }

    float result = p->to_min + t * (p->to_max - p->to_min);
    write_channel_(inout, p->to_channel, result);
}
