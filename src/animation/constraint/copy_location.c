/**
 * @file copy_location.c
 * @brief Copy Location and Copy Scale constraint evaluators.
 *
 * Non-static functions: 2 (eval_copy_location, eval_copy_scale)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/**
 * @brief Copy Location evaluator.
 *
 * Copies the translation from the target bone to the owner bone,
 * with per-axis masking, inversion, and offset mode support.
 */
void eval_copy_location(const constraint_def_t *def,
                        const constraint_eval_ctx_t *ctx,
                        mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    const mat4_t *target = &ctx->pose[def->target_bone_idx];
    const constraint_copy_location_params_t *p = &def->params.copy_location;

    float target_pos[3] = { target->m[12], target->m[13], target->m[14] };
    float owner_pos[3]  = { inout->m[12], inout->m[13], inout->m[14] };

    /* Apply inversion. */
    if (p->invert_x) target_pos[0] = -target_pos[0];
    if (p->invert_y) target_pos[1] = -target_pos[1];
    if (p->invert_z) target_pos[2] = -target_pos[2];

    float result[3];
    if (p->offset) {
        /* Additive: owner + target. */
        result[0] = owner_pos[0] + target_pos[0];
        result[1] = owner_pos[1] + target_pos[1];
        result[2] = owner_pos[2] + target_pos[2];
    } else {
        /* Replace. */
        result[0] = target_pos[0];
        result[1] = target_pos[1];
        result[2] = target_pos[2];
    }

    /* Apply per-axis masking: only modify enabled axes. */
    if (p->use_x) inout->m[12] = result[0];
    if (p->use_y) inout->m[13] = result[1];
    if (p->use_z) inout->m[14] = result[2];
}

/**
 * @brief Copy Scale evaluator.
 *
 * Copies the scale from the target bone to the owner bone.
 * Scale is extracted as column magnitudes. Supports per-axis masking,
 * power exponent, and additive mode.
 */
void eval_copy_scale(const constraint_def_t *def,
                     const constraint_eval_ctx_t *ctx,
                     mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    const mat4_t *target = &ctx->pose[def->target_bone_idx];
    const constraint_copy_scale_params_t *p = &def->params.copy_scale;

    /* Extract target scale from column lengths. */
    float target_scale[3];
    for (int col = 0; col < 3; col++) {
        float x = target->m[col * 4 + 0];
        float y = target->m[col * 4 + 1];
        float z = target->m[col * 4 + 2];
        target_scale[col] = sqrtf(x * x + y * y + z * z);
    }

    /* Apply power exponent. */
    float power = p->power;
    if (power != 1.0f && power != 0.0f) {
        for (int i = 0; i < 3; i++) {
            target_scale[i] = powf(target_scale[i], power);
        }
    }

    /* Extract owner rotation (normalized columns). */
    float owner_rot[3][3];
    float owner_scale[3];
    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        owner_scale[col] = sqrtf(x * x + y * y + z * z);
        if (owner_scale[col] < 1e-7f) owner_scale[col] = 1.0f;
        owner_rot[col][0] = x / owner_scale[col];
        owner_rot[col][1] = y / owner_scale[col];
        owner_rot[col][2] = z / owner_scale[col];
    }

    /* Compute final scale per axis. */
    float final_scale[3] = { owner_scale[0], owner_scale[1], owner_scale[2] };

    const bool axes[3] = { p->use_x, p->use_y, p->use_z };
    for (int i = 0; i < 3; i++) {
        if (axes[i]) {
            if (p->offset) {
                final_scale[i] = owner_scale[i] + target_scale[i];
            } else {
                final_scale[i] = target_scale[i];
            }
        }
    }

    /* Apply new scale to owner rotation. */
    for (int col = 0; col < 3; col++) {
        inout->m[col * 4 + 0] = owner_rot[col][0] * final_scale[col];
        inout->m[col * 4 + 1] = owner_rot[col][1] * final_scale[col];
        inout->m[col * 4 + 2] = owner_rot[col][2] * final_scale[col];
    }
}
