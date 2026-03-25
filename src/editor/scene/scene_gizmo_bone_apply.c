/**
 * @file scene_gizmo_bone_apply.c
 * @brief Per-bone gizmo apply: translate and rotate bone rest poses.
 *
 * Both operations modify rest_local and then propagate rest_world
 * through the bone hierarchy so children follow their parent.
 *
 * Non-static functions (2 / 4-function rule):
 *   1. per_bone_gizmo_apply_drag
 *   2. per_bone_gizmo_apply_rotate
 */

#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stddef.h>

/**
 * @brief Recompute rest_world for a bone and all its descendants.
 *
 * rest_world[i] = rest_world[parent[i]] * rest_local[i]
 * Root bones (parent == UINT32_MAX) use rest_local directly.
 *
 * Also updates tail_positions from the new rest_world.
 */
static void propagate_rest_world(skeleton_def_t *skel, uint32_t bone_index) {
    if (!skel->rest_local || !skel->rest_world || !skel->parent_indices) {
        return;
    }

    /* Save old head position and rotation before overwriting rest_world. */
    float old_hx = skel->rest_world[bone_index].m[12];
    float old_hy = skel->rest_world[bone_index].m[13];
    float old_hz = skel->rest_world[bone_index].m[14];

    /* Compute local-space tail offset using old rest_world rotation.
     * local_offset = transpose(old_rotation) * (tail_world - head_world). */
    float local_tail[3] = {0.0f, 0.0f, 0.0f};
    bool has_tail = (skel->tail_positions != NULL);
    if (has_tail) {
        float dx = skel->tail_positions[bone_index * 3 + 0] - old_hx;
        float dy = skel->tail_positions[bone_index * 3 + 1] - old_hy;
        float dz = skel->tail_positions[bone_index * 3 + 2] - old_hz;
        const float *m = skel->rest_world[bone_index].m;
        /* Transpose of 3x3 rotation: row i = column i of original. */
        local_tail[0] = m[0] * dx + m[1] * dy + m[2]  * dz;
        local_tail[1] = m[4] * dx + m[5] * dy + m[6]  * dz;
        local_tail[2] = m[8] * dx + m[9] * dy + m[10] * dz;
    }

    /* Recompute this bone's rest_world. */
    uint32_t parent = skel->parent_indices[bone_index];
    if (parent == UINT32_MAX || parent >= skel->joint_count) {
        skel->rest_world[bone_index] = skel->rest_local[bone_index];
    } else {
        skel->rest_world[bone_index] = mat4_mul(
            skel->rest_world[parent], skel->rest_local[bone_index]);
    }

    /* Recompute tail_position from new rest_world rotation * local offset. */
    if (has_tail) {
        const float *m = skel->rest_world[bone_index].m;
        float new_hx = m[12];
        float new_hy = m[13];
        float new_hz = m[14];
        skel->tail_positions[bone_index * 3 + 0] = new_hx
            + m[0] * local_tail[0] + m[4] * local_tail[1] + m[8]  * local_tail[2];
        skel->tail_positions[bone_index * 3 + 1] = new_hy
            + m[1] * local_tail[0] + m[5] * local_tail[1] + m[9]  * local_tail[2];
        skel->tail_positions[bone_index * 3 + 2] = new_hz
            + m[2] * local_tail[0] + m[6] * local_tail[1] + m[10] * local_tail[2];
    }

    /* Recurse into children. */
    for (uint32_t c = 0; c < skel->joint_count; c++) {
        if (skel->parent_indices[c] == bone_index) {
            propagate_rest_world(skel, c);
        }
    }
}

void per_bone_gizmo_apply_drag(
    struct skeleton_def *skel,
    uint32_t bone_index,
    vec3_t delta)
{
    if (!skel) { return; }
    if (bone_index >= skel->joint_count) { return; }

    /* Translate rest_local (translation in m[12..14]). */
    if (skel->rest_local) {
        skel->rest_local[bone_index].m[12] += delta.x;
        skel->rest_local[bone_index].m[13] += delta.y;
        skel->rest_local[bone_index].m[14] += delta.z;
    }

    /* Propagate rest_world and tail_positions for this bone and descendants. */
    propagate_rest_world(skel, bone_index);
}

void per_bone_gizmo_apply_rotate(
    struct skeleton_def *skel,
    uint32_t bone_index,
    quat_t dq)
{
    if (!skel) { return; }
    if (bone_index >= skel->joint_count) { return; }

    mat4_t *rl = skel->rest_local
                     ? &skel->rest_local[bone_index]
                     : &skel->rest_world[bone_index];

    /* Save translation. */
    float tx = rl->m[12];
    float ty = rl->m[13];
    float tz = rl->m[14];

    /* Extract per-column scale from the 3x3 part of rest_local.
     * This preserves any baked scale from the skeleton exporter. */
    float sx = sqrtf(rl->m[0]*rl->m[0] + rl->m[1]*rl->m[1] + rl->m[2]*rl->m[2]);
    float sy = sqrtf(rl->m[4]*rl->m[4] + rl->m[5]*rl->m[5] + rl->m[6]*rl->m[6]);
    float sz = sqrtf(rl->m[8]*rl->m[8] + rl->m[9]*rl->m[9] + rl->m[10]*rl->m[10]);
    if (sx < 1e-8f) sx = 1.0f;
    if (sy < 1e-8f) sy = 1.0f;
    if (sz < 1e-8f) sz = 1.0f;

    /* Extract current rotation as quaternion (from scale-stripped matrix). */
    mat4_t rot_only = *rl;
    rot_only.m[0] /= sx; rot_only.m[1] /= sx; rot_only.m[2] /= sx;
    rot_only.m[4] /= sy; rot_only.m[5] /= sy; rot_only.m[6] /= sy;
    rot_only.m[8] /= sz; rot_only.m[9] /= sz; rot_only.m[10] /= sz;
    quat_t cur_q = quat_from_mat4(&rot_only);

    /* Compose: new_rotation = cur_rotation * dq (post-multiply = local-space). */
    quat_t new_q = quat_normalize_safe(quat_mul(cur_q, dq), 1e-8f);

    /* Rebuild rest_local = new_rotation * scale. */
    mat4_t new_rot;
    quat_to_mat4(new_q, &new_rot);
    rl->m[0]  = new_rot.m[0] * sx; rl->m[1]  = new_rot.m[1] * sx; rl->m[2]  = new_rot.m[2] * sx;
    rl->m[4]  = new_rot.m[4] * sy; rl->m[5]  = new_rot.m[5] * sy; rl->m[6]  = new_rot.m[6] * sy;
    rl->m[8]  = new_rot.m[8] * sz; rl->m[9]  = new_rot.m[9] * sz; rl->m[10] = new_rot.m[10] * sz;

    /* Restore translation. */
    rl->m[12] = tx;
    rl->m[13] = ty;
    rl->m[14] = tz;

    /* Propagate rest_world and tail_positions for this bone and all
     * descendants. propagate_rest_world handles tail via local offset. */
    if (skel->rest_local) {
        propagate_rest_world(skel, bone_index);
    }
}
