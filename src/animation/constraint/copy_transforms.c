/**
 * @file copy_transforms.c
 * @brief Copy Transforms and Copy Rotation constraint evaluators.
 *
 * Non-static functions: 2 (eval_copy_transforms, eval_copy_rotation)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/**
 * @brief Extract the 3x3 rotation submatrix from a mat4, discarding
 *        scale by normalizing each column.
 */
static void extract_rotation_(const mat4_t *src, float rot[9]) {
    for (int col = 0; col < 3; col++) {
        float x = src->m[col * 4 + 0];
        float y = src->m[col * 4 + 1];
        float z = src->m[col * 4 + 2];
        float len = sqrtf(x * x + y * y + z * z);
        if (len < 1e-7f) len = 1e-7f;
        rot[col * 3 + 0] = x / len;
        rot[col * 3 + 1] = y / len;
        rot[col * 3 + 2] = z / len;
    }
}

/**
 * @brief Copy Transforms evaluator (REPLACE mode).
 *
 * Copies the full transform from the target bone to the owner bone.
 */
void eval_copy_transforms(const constraint_def_t *def,
                          const constraint_eval_ctx_t *ctx,
                          mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    /* REPLACE mode: just copy the target transform. */
    *inout = ctx->pose[def->target_bone_idx];
}

/**
 * @brief Copy Rotation evaluator.
 *
 * Copies the rotation part of the target's transform to the owner,
 * with per-axis masking and inversion. Preserves the owner's translation
 * and scale.
 */
void eval_copy_rotation(const constraint_def_t *def,
                        const constraint_eval_ctx_t *ctx,
                        mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    const mat4_t *target = &ctx->pose[def->target_bone_idx];

    /* Extract target rotation (normalized columns). */
    float target_rot[9];
    extract_rotation_(target, target_rot);

    /* Extract owner scale (column lengths). */
    float owner_scale[3];
    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        owner_scale[col] = sqrtf(x * x + y * y + z * z);
        if (owner_scale[col] < 1e-7f) owner_scale[col] = 1.0f;
    }

    /* Preserve owner translation. */
    float tx = inout->m[12], ty = inout->m[13], tz = inout->m[14];

    /* Apply target rotation with owner scale. */
    const constraint_copy_rotation_params_t *p = &def->params.copy_rotation;

    if (p->use_x && p->use_y && p->use_z &&
        !p->invert_x && !p->invert_y && !p->invert_z) {
        /* Fast path: full copy. */
        for (int col = 0; col < 3; col++) {
            inout->m[col * 4 + 0] = target_rot[col * 3 + 0] * owner_scale[col];
            inout->m[col * 4 + 1] = target_rot[col * 3 + 1] * owner_scale[col];
            inout->m[col * 4 + 2] = target_rot[col * 3 + 2] * owner_scale[col];
        }
    } else {
        /* Per-axis masking: blend between owner and target rotation columns. */
        float owner_rot[9];
        extract_rotation_(inout, owner_rot);

        /* For simplicity in REPLACE mode, replace columns per axis flags. */
        float final_rot[9];
        for (int i = 0; i < 9; i++) final_rot[i] = owner_rot[i];

        /* Replace rotation columns that are enabled. */
        if (p->use_x) {
            float inv = p->invert_x ? -1.0f : 1.0f;
            for (int col = 0; col < 3; col++) {
                final_rot[col * 3 + 0] = target_rot[col * 3 + 0] * inv;
            }
        }
        if (p->use_y) {
            float inv = p->invert_y ? -1.0f : 1.0f;
            for (int col = 0; col < 3; col++) {
                final_rot[col * 3 + 1] = target_rot[col * 3 + 1] * inv;
            }
        }
        if (p->use_z) {
            float inv = p->invert_z ? -1.0f : 1.0f;
            for (int col = 0; col < 3; col++) {
                final_rot[col * 3 + 2] = target_rot[col * 3 + 2] * inv;
            }
        }

        for (int col = 0; col < 3; col++) {
            inout->m[col * 4 + 0] = final_rot[col * 3 + 0] * owner_scale[col];
            inout->m[col * 4 + 1] = final_rot[col * 3 + 1] * owner_scale[col];
            inout->m[col * 4 + 2] = final_rot[col * 3 + 2] * owner_scale[col];
        }
    }

    /* Restore owner translation. */
    inout->m[12] = tx;
    inout->m[13] = ty;
    inout->m[14] = tz;
}
