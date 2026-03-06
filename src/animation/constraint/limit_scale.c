/**
 * @file limit_scale.c
 * @brief Limit Scale constraint evaluator.
 *
 * Clamps bone scale to per-axis min/max bounds, preserving rotation
 * and translation.
 *
 * Non-static functions: 1 (eval_limit_scale)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/**
 * @brief Limit Scale evaluator.
 *
 * Extracts per-column scale, clamps to specified ranges, and applies
 * the clamped scale back to the rotation columns.
 */
void eval_limit_scale(const constraint_def_t *def,
                      const constraint_eval_ctx_t *ctx,
                      mat4_t *inout) {
    (void)ctx;
    if (!def || !inout) return;

    const constraint_limit_scale_params_t *p = &def->params.limit_scale;

    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        float len = sqrtf(x * x + y * y + z * z);
        if (len < 1e-7f) continue;

        float new_len = len;
        if (col == 0) {
            if (p->use_min_x && new_len < p->min_x) new_len = p->min_x;
            if (p->use_max_x && new_len > p->max_x) new_len = p->max_x;
        } else if (col == 1) {
            if (p->use_min_y && new_len < p->min_y) new_len = p->min_y;
            if (p->use_max_y && new_len > p->max_y) new_len = p->max_y;
        } else {
            if (p->use_min_z && new_len < p->min_z) new_len = p->min_z;
            if (p->use_max_z && new_len > p->max_z) new_len = p->max_z;
        }

        if (new_len != len) {
            float ratio = new_len / len;
            inout->m[col * 4 + 0] *= ratio;
            inout->m[col * 4 + 1] *= ratio;
            inout->m[col * 4 + 2] *= ratio;
        }
    }
}
