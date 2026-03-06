/**
 * @file ik_register.c
 * @brief Register IK constraint evaluators in the solver dispatch table.
 *
 * Non-static functions: 1 (ik_solver_register)
 */

#include "ferrum/animation/ik_solver.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

/**
 * @brief IK constraint evaluator — dispatches to CCD solver.
 *
 * Reads chain_length, iterations, and target from the constraint def/ctx.
 * If a target_bone_idx is specified, uses that bone's position as the target;
 * otherwise defaults to world origin (0,0,0).
 */
static void ik_eval_fn_(const constraint_def_t *def,
                         const constraint_eval_ctx_t *ctx,
                         mat4_t *inout_transform) {
    (void)inout_transform; /* CCD modifies pose directly. */

    if (!def || !ctx || !ctx->skel || !ctx->pose) return;

    /* Get target position from target bone or world origin. */
    vec3_t target = { 0.0f, 0.0f, 0.0f };
    if (def->target_bone_idx != UINT32_MAX &&
        def->target_bone_idx < ctx->bone_count) {
        target.x = ctx->pose[def->target_bone_idx].m[12];
        target.y = ctx->pose[def->target_bone_idx].m[13];
        target.z = ctx->pose[def->target_bone_idx].m[14];
    }

    /* CCD solver operates on the full pose array. We need a mutable copy. */
    /* The solver provides const pose. IK is a special case that modifies
     * the pose in place. We cast away const here (the solver framework
     * should be updated eventually to support IK's global pose modification). */
    mat4_t *mutable_pose = (mat4_t *)ctx->pose;

    ik_solve_ccd(ctx->skel, mutable_pose,
                 ctx->bone_count, def->params.ik.chain_length,
                 target, def->params.ik.iterations, 0.001f);
}

void ik_solver_register(constraint_solver_t *solver) {
    if (!solver) return;
    constraint_solver_register_eval(solver, CONSTRAINT_IK, ik_eval_fn_);
}
