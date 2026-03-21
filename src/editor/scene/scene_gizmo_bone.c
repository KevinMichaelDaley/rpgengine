/**
 * @file scene_gizmo_bone.c
 * @brief Per-bone gizmo build and pick.
 *
 * Non-static functions (2 / 4-function rule):
 *   1. per_bone_gizmo_build
 *   2. per_bone_gizmo_pick
 */

#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <math.h>

uint32_t per_bone_gizmo_build(
    const struct skeleton_def *skel,
    const struct edit_bone_selection *bone_sel,
    const struct mat4 *entity_model,
    gizmo_mode_t mode,
    per_bone_gizmo_t *out,
    uint32_t capacity)
{
    if (!skel || !bone_sel || !entity_model || !out || capacity == 0) {
        return 0;
    }

    uint32_t bone_count = 0;
    const uint32_t *bones = edit_bone_selection_bones(bone_sel, &bone_count);
    if (!bones || bone_count == 0) {
        return 0;
    }

    uint32_t written = 0;
    for (uint32_t i = 0; i < bone_count && written < capacity; i++) {
        uint32_t bi = bones[i];
        if (bi >= skel->joint_count) { continue; }

        per_bone_gizmo_t *g = &out[written];
        g->bone_index = bi;

        gizmo_state_init(&g->gizmo);
        gizmo_state_set_mode(&g->gizmo, mode);

        /* Bone head position from rest_world translation. */
        float bx = skel->rest_world[bi].m[12];
        float by = skel->rest_world[bi].m[13];
        float bz = skel->rest_world[bi].m[14];

        /* Transform by entity model matrix. */
        const float *em = entity_model->m;
        g->gizmo.position.x = em[0]*bx + em[4]*by + em[8]*bz  + em[12];
        g->gizmo.position.y = em[1]*bx + em[5]*by + em[9]*bz  + em[13];
        g->gizmo.position.z = em[2]*bx + em[6]*by + em[10]*bz + em[14];

        written++;
    }

    return written;
}

int32_t per_bone_gizmo_pick(
    const per_bone_gizmo_t *gizmos,
    uint32_t count,
    const struct editor_ray *ray,
    float gizmo_scale)
{
    if (!gizmos || count == 0 || !ray) {
        return -1;
    }

    /* Proximity-based pick: find the gizmo whose center is closest
     * to the ray, within a scaled hit radius.  The hit radius is
     * proportional to gizmo_scale so the clickable area matches the
     * visual gizmo size on screen. */
    float hit_radius = gizmo_scale * 0.15f;
    if (hit_radius < 0.01f) { hit_radius = 0.01f; }
    float best_dist = hit_radius;
    int32_t best_idx = -1;

    for (uint32_t i = 0; i < count; i++) {
        /* Vector from ray origin to gizmo position. */
        vec3_t to_gizmo = {
            gizmos[i].gizmo.position.x - ray->origin.x,
            gizmos[i].gizmo.position.y - ray->origin.y,
            gizmos[i].gizmo.position.z - ray->origin.z,
        };

        /* Project onto ray direction to find closest point on ray. */
        float t = to_gizmo.x * ray->direction.x
                + to_gizmo.y * ray->direction.y
                + to_gizmo.z * ray->direction.z;
        if (t < 0.0f) { continue; } /* Behind ray origin. */

        /* Perpendicular distance from gizmo center to ray. */
        float dx = to_gizmo.x - t * ray->direction.x;
        float dy = to_gizmo.y - t * ray->direction.y;
        float dz = to_gizmo.z - t * ray->direction.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);

        if (dist < best_dist) {
            best_dist = dist;
            best_idx = (int32_t)i;
        }
    }

    return best_idx;
}
