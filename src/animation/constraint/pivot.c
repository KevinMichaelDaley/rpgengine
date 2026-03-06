/**
 * @file pivot.c
 * @brief Pivot constraint evaluator.
 *
 * Offsets the rotation center by a specified pivot point.
 * result = T(pivot) × R × T(-pivot) where R is the current rotation.
 *
 * Non-static functions: 1 (eval_pivot)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/**
 * @brief Pivot constraint evaluator.
 *
 * Applies the bone's rotation around an offset pivot point instead
 * of around the bone's position. The rotation_range threshold can
 * deactivate the constraint when rotation is small.
 */
void eval_pivot(const constraint_def_t *def,
                const constraint_eval_ctx_t *ctx,
                mat4_t *inout) {
    (void)ctx;
    if (!def || !inout) return;

    const constraint_pivot_params_t *p = &def->params.pivot;

    /* Check rotation_range threshold: if set, only activate when
     * rotation angle exceeds the threshold. Compute approximate
     * rotation angle from trace of upper 3x3. */
    if (p->rotation_range > 0.0f) {
        float trace = inout->m[0] + inout->m[5] + inout->m[10];
        float cos_angle = (trace - 1.0f) * 0.5f;
        if (cos_angle > 1.0f) cos_angle = 1.0f;
        if (cos_angle < -1.0f) cos_angle = -1.0f;
        float angle = acosf(cos_angle);
        if (angle < p->rotation_range) return; /* below threshold */
    }

    float px = p->offset[0];
    float py = p->offset[1];
    float pz = p->offset[2];

    /* Extract current translation. */
    float tx = inout->m[12];
    float ty = inout->m[13];
    float tz = inout->m[14];

    /* Compute T(pivot) × R × T(-pivot):
     * New translation = R × (-pivot) + pivot + original_translation
     * = -(R × pivot) + pivot + T */
    float rpx = inout->m[0] * (-px) + inout->m[4] * (-py) + inout->m[8]  * (-pz);
    float rpy = inout->m[1] * (-px) + inout->m[5] * (-py) + inout->m[9]  * (-pz);
    float rpz = inout->m[2] * (-px) + inout->m[6] * (-py) + inout->m[10] * (-pz);

    inout->m[12] = rpx + px + tx;
    inout->m[13] = rpy + py + ty;
    inout->m[14] = rpz + pz + tz;
}
