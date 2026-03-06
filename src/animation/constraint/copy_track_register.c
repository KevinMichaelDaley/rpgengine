/**
 * @file copy_track_register.c
 * @brief Register all copy and tracking constraint evaluators.
 *
 * Non-static functions: 1 (copy_track_register)
 */

#include "ferrum/animation/copy_track.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/* External evaluators from copy_transforms.c */
extern void eval_copy_transforms(const constraint_def_t *def,
                                 const constraint_eval_ctx_t *ctx,
                                 mat4_t *inout);
extern void eval_copy_rotation(const constraint_def_t *def,
                               const constraint_eval_ctx_t *ctx,
                               mat4_t *inout);

/* External evaluators from copy_location.c */
extern void eval_copy_location(const constraint_def_t *def,
                               const constraint_eval_ctx_t *ctx,
                               mat4_t *inout);
extern void eval_copy_scale(const constraint_def_t *def,
                            const constraint_eval_ctx_t *ctx,
                            mat4_t *inout);

/* External evaluators from track_damped.c */
extern void eval_damped_track(const constraint_def_t *def,
                              const constraint_eval_ctx_t *ctx,
                              mat4_t *inout);

/* External evaluators from track_to.c */
extern void eval_track_to(const constraint_def_t *def,
                          const constraint_eval_ctx_t *ctx,
                          mat4_t *inout);
extern void eval_locked_track(const constraint_def_t *def,
                              const constraint_eval_ctx_t *ctx,
                              mat4_t *inout);

void copy_track_register(constraint_solver_t *solver) {
    if (!solver) return;
    constraint_solver_register_eval(solver, CONSTRAINT_COPY_TRANSFORMS, eval_copy_transforms);
    constraint_solver_register_eval(solver, CONSTRAINT_COPY_ROTATION,   eval_copy_rotation);
    constraint_solver_register_eval(solver, CONSTRAINT_COPY_LOCATION,   eval_copy_location);
    constraint_solver_register_eval(solver, CONSTRAINT_COPY_SCALE,      eval_copy_scale);
    constraint_solver_register_eval(solver, CONSTRAINT_DAMPED_TRACK,    eval_damped_track);
    constraint_solver_register_eval(solver, CONSTRAINT_TRACK_TO,        eval_track_to);
    constraint_solver_register_eval(solver, CONSTRAINT_LOCKED_TRACK,    eval_locked_track);
}
