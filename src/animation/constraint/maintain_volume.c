/**
 * @file maintain_volume.c
 * @brief Maintain Volume constraint evaluator.
 *
 * Compensates scale on non-free axes to preserve volume when the
 * free axis scales. If free axis scales by S, the other two axes
 * scale by 1/sqrt(S) to maintain V = Sx * Sy * Sz = constant.
 *
 * Non-static functions: 1 (eval_maintain_volume)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include <math.h>

/**
 * @brief Maintain Volume evaluator.
 */
void eval_maintain_volume(const constraint_def_t *def,
                          const constraint_eval_ctx_t *ctx,
                          mat4_t *inout) {
    (void)ctx;
    if (!def || !inout) return;

    const constraint_maintain_volume_params_t *p = &def->params.maintain_volume;

    /* Determine free axis column index. */
    int free_col;
    switch (p->free_axis) {
        case CONSTRAINT_AXIS_X: free_col = 0; break;
        case CONSTRAINT_AXIS_Y: free_col = 1; break;
        case CONSTRAINT_AXIS_Z: free_col = 2; break;
        default: return;
    }

    /* Extract scale (column magnitudes). */
    float scale[3];
    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        scale[col] = sqrtf(x * x + y * y + z * z);
        if (scale[col] < 1e-7f) scale[col] = 1e-7f;
    }

    float free_scale = scale[free_col];
    float ref_volume = p->volume > 0.0f ? p->volume : 1.0f;

    /* To maintain V = ref_volume when free axis = free_scale:
     * compensated = ref_volume / free_scale
     * Each of the other two axes scales by sqrt(compensated).
     * Since original other axes were 1.0 (ref), new scale = sqrt(ref/free). */
    float compensated = ref_volume / free_scale;
    if (compensated < 1e-7f) compensated = 1e-7f;
    float comp_scale = sqrtf(compensated);

    /* Apply compensated scale to non-free axes. */
    for (int col = 0; col < 3; col++) {
        if (col == free_col) continue;
        float ratio = comp_scale / scale[col];
        inout->m[col * 4 + 0] *= ratio;
        inout->m[col * 4 + 1] *= ratio;
        inout->m[col * 4 + 2] *= ratio;
    }
}
