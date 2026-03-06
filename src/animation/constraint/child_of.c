/**
 * @file child_of.c
 * @brief Child Of constraint evaluator.
 *
 * Dynamic re-parenting: makes the owner's transform relative to the
 * target's transform, with per-channel enable/disable.
 *
 * Non-static functions: 1 (eval_child_of)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/**
 * @brief Child Of constraint evaluator.
 *
 * owner_world = target_world × inverse_matrix × owner_local
 * With per-channel masking on the result.
 */
void eval_child_of(const constraint_def_t *def,
                   const constraint_eval_ctx_t *ctx,
                   mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    const constraint_child_of_params_t *p = &def->params.child_of;
    const mat4_t *target = &ctx->pose[def->target_bone_idx];

    /* Compute: result = target × inverse. */
    mat4_t result = mat4_mul(*target, p->inverse_matrix);

    /* Apply per-channel masking: only update enabled channels. */
    float owner_pos[3] = { inout->m[12], inout->m[13], inout->m[14] };

    if (p->use_location_x) inout->m[12] = result.m[12]; else inout->m[12] = owner_pos[0];
    if (p->use_location_y) inout->m[13] = result.m[13]; else inout->m[13] = owner_pos[1];
    if (p->use_location_z) inout->m[14] = result.m[14]; else inout->m[14] = owner_pos[2];

    /* Rotation channels: apply rotation part if all axes enabled. */
    if (p->use_rotation_x && p->use_rotation_y && p->use_rotation_z) {
        /* Copy rotation (upper 3x3) from result, preserving owner's scale. */
        for (int col = 0; col < 3; col++) {
            if ((col == 0 && p->use_scale_x) ||
                (col == 1 && p->use_scale_y) ||
                (col == 2 && p->use_scale_z)) {
                inout->m[col * 4 + 0] = result.m[col * 4 + 0];
                inout->m[col * 4 + 1] = result.m[col * 4 + 1];
                inout->m[col * 4 + 2] = result.m[col * 4 + 2];
            } else {
                /* Copy rotation without scale from result, preserve owner scale. */
                float result_len = 0.0f;
                for (int r = 0; r < 3; r++) result_len += result.m[col*4+r] * result.m[col*4+r];
                result_len = (result_len > 1e-14f) ? 1.0f : 0.0f; /* just check for zero */
                if (result_len > 0.0f) {
                    inout->m[col * 4 + 0] = result.m[col * 4 + 0];
                    inout->m[col * 4 + 1] = result.m[col * 4 + 1];
                    inout->m[col * 4 + 2] = result.m[col * 4 + 2];
                }
            }
        }
    }
}
