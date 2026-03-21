/**
 * @file scene_viewport_bone_overlay_params.c
 * @brief Bone capsule geometry computation from joint positions.
 *
 * Non-static functions (1 / 4 limit):
 *   bone_capsule_params_from_joint
 */

#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/math/vec3.h"

#include <math.h>

void bone_capsule_params_from_joint(const float *head, const float *tail,
                                     bone_capsule_params_t *out_params) {
    if (!head || !tail || !out_params) return;

    /* Compute midpoint. */
    out_params->center[0] = (head[0] + tail[0]) * 0.5f;
    out_params->center[1] = (head[1] + tail[1]) * 0.5f;
    out_params->center[2] = (head[2] + tail[2]) * 0.5f;

    /* Compute direction and length. */
    float dx = tail[0] - head[0];
    float dy = tail[1] - head[1];
    float dz = tail[2] - head[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);

    /* The capsule mesh extends beyond the line segment by radius on each
     * end (hemispherical caps). Shorten the cylinder portion so the total
     * capsule height (cylinder + 2*caps) equals the bone length. */
    float radius = len * BONE_CAPSULE_RADIUS_FACTOR;
    if (radius < BONE_CAPSULE_MIN_RADIUS) radius = BONE_CAPSULE_MIN_RADIUS;
    if (radius > BONE_CAPSULE_MAX_RADIUS) radius = BONE_CAPSULE_MAX_RADIUS;

    float cyl_len = len - 2.0f * radius;
    if (cyl_len < 0.01f) cyl_len = 0.01f;
    out_params->length = cyl_len;

    /* Compute axis (unit direction head→tail). */
    if (len > 1e-6f) {
        float inv_len = 1.0f / len;
        out_params->axis[0] = dx * inv_len;
        out_params->axis[1] = dy * inv_len;
        out_params->axis[2] = dz * inv_len;
    } else {
        /* Degenerate: default to Y-up axis. */
        out_params->axis[0] = 0.0f;
        out_params->axis[1] = 1.0f;
        out_params->axis[2] = 0.0f;
    }

    out_params->radius = radius;
}
