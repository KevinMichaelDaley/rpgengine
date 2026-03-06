/**
 * @file floor.c
 * @brief Floor constraint evaluator.
 *
 * Prevents the owner from crossing a plane defined by the target's
 * position. Maps directly to physics contact constraints.
 *
 * Non-static functions: 1 (eval_floor)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/**
 * @brief Floor constraint evaluator.
 *
 * Clamps the owner's position so it does not cross the floor plane.
 * Currently supports FLOOR_BELOW_NEG_Y: floor is at the target's Y position.
 */
void eval_floor(const constraint_def_t *def,
                const constraint_eval_ctx_t *ctx,
                mat4_t *inout) {
    if (!def || !ctx || !inout) return;
    if (def->target_bone_idx == UINT32_MAX) return;
    if (def->target_bone_idx >= ctx->bone_count) return;

    const constraint_floor_params_t *p = &def->params.floor;
    const mat4_t *target = &ctx->pose[def->target_bone_idx];

    /* Get floor height from target's Y position. */
    float floor_y = target->m[13] + p->offset;

    /* For FLOOR_BELOW_NEG_Y: owner must not go below floor_y. */
    if (p->floor_location == CONSTRAINT_FLOOR_BELOW_NEG_Y ||
        p->floor_location == 0) { /* default */
        if (inout->m[13] < floor_y) {
            inout->m[13] = floor_y;
        }
    }
}
