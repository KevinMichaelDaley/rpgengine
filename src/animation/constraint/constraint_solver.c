/**
 * @file constraint_solver.c
 * @brief Constraint solver: init, destroy, evaluate loop.
 *
 * Non-static functions: 3 (init, destroy, evaluate)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_space.h"
#include <stdlib.h>
#include <string.h>

/* ── No-op stub for unregistered constraint types ────────────────── */

static void noop_eval_(const constraint_def_t *def,
                       const constraint_eval_ctx_t *ctx,
                       mat4_t *inout) {
    (void)def;
    (void)ctx;
    (void)inout;
}

bool constraint_solver_init(constraint_solver_t *solver, uint32_t max_bones,
                            uint32_t max_constraints) {
    if (!solver || max_bones == 0) return false;

    memset(solver, 0, sizeof(*solver));
    solver->max_bones = max_bones;
    solver->max_constraints_per_bone = max_constraints;

    /* Initialize all dispatch entries to no-op. */
    for (int i = 0; i < CONSTRAINT_TYPE_COUNT; i++) {
        solver->dispatch[i] = noop_eval_;
    }

    /* Allocate workspace (one mat4 per bone for pre-constraint snapshot). */
    solver->workspace = calloc(max_bones, sizeof(mat4_t));
    if (!solver->workspace) {
        return false;
    }

    return true;
}

void constraint_solver_destroy(constraint_solver_t *solver) {
    if (!solver) return;
    free(solver->workspace);
    memset(solver, 0, sizeof(*solver));
}

void constraint_solver_evaluate(constraint_solver_t *solver,
                                const skeleton_def_t *skel,
                                const mat4_t *local_pose,
                                mat4_t *pose, uint32_t bone_count) {
    if (!solver || !skel || !pose || bone_count == 0) return;

    /* Cap to solver capacity. */
    uint32_t count = bone_count < solver->max_bones ? bone_count : solver->max_bones;

    constraint_eval_ctx_t ctx;
    ctx.skel = skel;
    ctx.pose = pose;
    ctx.bone_count = count;

    /* Evaluate in skeleton order (parents before children). */
    for (uint32_t bi = 0; bi < count; bi++) {
        /* FK propagate from parent before evaluating constraints.
         * This ensures each bone's world transform reflects any
         * modifications made to ancestor bones by prior constraints. */
        if (local_pose) {
            uint32_t pi = skel->parent_indices[bi];
            if (pi != UINT32_MAX && pi < count) {
                pose[bi] = mat4_mul(pose[pi], local_pose[bi]);
            } else {
                pose[bi] = local_pose[bi];
            }
        }

        uint32_t num_constraints = skel->constraint_counts[bi];
        if (num_constraints == 0) continue;

        ctx.bone_idx = bi;

        /* Evaluate each constraint in stack order. */
        for (uint32_t ci = 0; ci < num_constraints; ci++) {
            const constraint_def_t *def =
                &skel->constraints[bi * skel->max_constraints_per_joint + ci];

            if (!constraint_type_is_valid(def->type)) continue;

            /* Snapshot the pre-constraint transform. */
            mat4_t pre = pose[bi];

            /* Call the type-specific evaluator. */
            constraint_eval_fn fn = solver->dispatch[def->type];
            fn(def, &ctx, &pose[bi]);

            /* Apply influence blending. */
            if (def->influence < 1.0f) {
                mat4_t blended;
                constraint_blend_influence(&pre, &pose[bi], def->influence, &blended);
                pose[bi] = blended;
            }
        }
    }
}
