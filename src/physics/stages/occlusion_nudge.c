/**
 * @file occlusion_nudge.c
 * @brief Position nudge for re-promoted bodies after occlusion.
 *
 * Lerps each flagged body's position toward a target over nudge_frames,
 * with a total correction cap of 5 mm (0.005 m).
 */

#include "ferrum/physics/occlusion_nudge.h"

#include <math.h>

#include "ferrum/physics/body.h"

/** Maximum total correction in meters. */
#define NUDGE_MAX_CORRECTION_M 0.005f

/**
 * @brief Apply position nudge for re-promoted bodies.
 */
void phys_occlusion_nudge_apply(phys_body_t *bodies,
                                const uint8_t *repromotion_flags,
                                const phys_vec3_t *target_positions,
                                uint32_t body_count,
                                uint32_t nudge_frames) {
    if (!bodies || !repromotion_flags || !target_positions) {
        return;
    }
    if (nudge_frames < 1) {
        nudge_frames = 1;
    }

    float max_step = NUDGE_MAX_CORRECTION_M / (float)nudge_frames;

    for (uint32_t i = 0; i < body_count; ++i) {
        if (!repromotion_flags[i]) {
            continue;
        }

        /* Compute delta from current to target. */
        float dx = target_positions[i].x - bodies[i].position.x;
        float dy = target_positions[i].y - bodies[i].position.y;
        float dz = target_positions[i].z - bodies[i].position.z;

        /* Uncapped step: 1/nudge_frames of the delta. */
        float fraction = 1.0f / (float)nudge_frames;
        float step_x = dx * fraction;
        float step_y = dy * fraction;
        float step_z = dz * fraction;

        /* Compute step magnitude. */
        float step_len = sqrtf(step_x * step_x +
                               step_y * step_y +
                               step_z * step_z);

        /* Cap per-frame step magnitude. */
        if (step_len > max_step && step_len > 0.0f) {
            float scale = max_step / step_len;
            step_x *= scale;
            step_y *= scale;
            step_z *= scale;
        }

        bodies[i].position.x += step_x;
        bodies[i].position.y += step_y;
        bodies[i].position.z += step_z;
    }
}
