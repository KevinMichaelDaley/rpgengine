/**
 * @file limit_location.c
 * @brief Limit Location constraint evaluator.
 *
 * Clamps bone position to per-axis min/max bounds.
 * Maps directly to physics distance joint limits.
 *
 * Non-static functions: 1 (eval_limit_location)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/**
 * @brief Limit Location evaluator.
 *
 * Clamps position per axis using independent min/max enable flags.
 */
void eval_limit_location(const constraint_def_t *def,
                         const constraint_eval_ctx_t *ctx,
                         mat4_t *inout) {
    (void)ctx;
    if (!def || !inout) return;

    const constraint_limit_location_params_t *p = &def->params.limit_location;

    if (p->use_min_x && inout->m[12] < p->min_x) inout->m[12] = p->min_x;
    if (p->use_max_x && inout->m[12] > p->max_x) inout->m[12] = p->max_x;
    if (p->use_min_y && inout->m[13] < p->min_y) inout->m[13] = p->min_y;
    if (p->use_max_y && inout->m[13] > p->max_y) inout->m[13] = p->max_y;
    if (p->use_min_z && inout->m[14] < p->min_z) inout->m[14] = p->min_z;
    if (p->use_max_z && inout->m[14] > p->max_z) inout->m[14] = p->max_z;
}
