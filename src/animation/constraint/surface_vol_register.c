/**
 * @file surface_vol_register.c
 * @brief Register surface and volume constraint evaluators.
 *
 * Non-static functions: 1 (surface_vol_register)
 */

#include "ferrum/animation/surface_vol.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/* External evaluators. */
extern void eval_floor(const constraint_def_t *def,
                       const constraint_eval_ctx_t *ctx,
                       mat4_t *inout);
extern void eval_maintain_volume(const constraint_def_t *def,
                                 const constraint_eval_ctx_t *ctx,
                                 mat4_t *inout);

void surface_vol_register(constraint_solver_t *solver) {
    if (!solver) return;
    constraint_solver_register_eval(solver, CONSTRAINT_FLOOR,           eval_floor);
    constraint_solver_register_eval(solver, CONSTRAINT_MAINTAIN_VOLUME, eval_maintain_volume);
    /* CONSTRAINT_CLAMP_TO and CONSTRAINT_SHRINKWRAP left as no-op stubs
     * (require curve/mesh query infrastructure). */
}
