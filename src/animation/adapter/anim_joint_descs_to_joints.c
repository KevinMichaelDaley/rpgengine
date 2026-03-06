/**
 * @file anim_joint_descs_to_joints.c
 * @brief Convert fskel bone_joint_desc_t entries to phys_joint_t.
 *
 * Iterates per-bone joint descriptors from the JNTS chunk and creates
 * physics joints for parent-child bone pairs.  Anchors are computed
 * from the current world-space bone poses.
 *
 * Non-static functions: 1 (anim_joint_descs_to_joints)
 */

#include "ferrum/animation/anim_constraint_rows.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/physics/joint.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

uint32_t anim_joint_descs_to_joints(
    const skeleton_def_t *skel,
    const mat4_t *world_pose,
    const uint32_t *bone_to_body_map,
    phys_joint_t *out_joints,
    uint32_t max_joints) {
    if (!skel || !skel->joints || !world_pose ||
        !bone_to_body_map || !out_joints) {
        return 0;
    }

    uint32_t count = 0;
    uint32_t n = skel->joint_count;

    for (uint32_t i = 0; i < n && count < max_joints; i++) {
        const bone_joint_desc_t *jd = &skel->joints[i];
        if (jd->joint_type == 0) continue;  /* NONE — skip. */

        uint32_t parent = skel->parent_indices[i];
        if (parent == UINT32_MAX || parent >= n) continue;

        uint32_t body_child  = bone_to_body_map[i];
        uint32_t body_parent = bone_to_body_map[parent];
        if (body_child == UINT32_MAX || body_parent == UINT32_MAX) continue;

        phys_joint_t *j = &out_joints[count];
        phys_joint_init(j);
        j->body_a = body_parent;
        j->body_b = body_child;

        /* Anchor at child bone position, in each body's local space.
         * world_delta is the vector from parent to child in world coords;
         * we must inverse-rotate by each body's orientation to get the
         * offset in body-local space. */
        float cx = world_pose[i].m[12];
        float cy = world_pose[i].m[13];
        float cz = world_pose[i].m[14];
        float px = world_pose[parent].m[12];
        float py = world_pose[parent].m[13];
        float pz = world_pose[parent].m[14];

        phys_quat_t parent_orient = quat_from_mat4(&world_pose[parent]);
        phys_vec3_t world_delta = {cx - px, cy - py, cz - pz};
        j->local_anchor_a = quat_inv_rotate_vec3(parent_orient, world_delta);
        j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

        switch (jd->joint_type) {
        case 1: /* Ball joint. */
            j->type = PHYS_JOINT_BALL;
            break;
        case 2: /* Hinge joint. */
            j->type = PHYS_JOINT_HINGE;
            /* Axis from exporter is in the child bone's local space.
             * Convert to parent-body-local via: world_axis = child_rot * axis,
             * then local_axis_a = inv(parent_rot) * world_axis. */
            {
                phys_quat_t child_orient = quat_from_mat4(&world_pose[i]);
                phys_vec3_t bone_axis = {
                    jd->axis[0], jd->axis[1], jd->axis[2]
                };
                phys_vec3_t world_axis = quat_rotate_vec3(
                    child_orient, bone_axis);
                j->local_axis_a = quat_inv_rotate_vec3(
                    parent_orient, world_axis);
            }
            /* Apply angular limits if specified. */
            if (jd->limit_min[0] != 0.0f || jd->limit_max[0] != 0.0f) {
                j->limit_min[0] = jd->limit_min[0];
                j->limit_max[0] = jd->limit_max[0];
                j->limit_axes = 1;
            }
            break;
        case 3: /* Distance joint. */
            j->type = PHYS_JOINT_DISTANCE;
            if (jd->rest_length > 0.0f) {
                j->rest_length = jd->rest_length;
            } else {
                /* Auto-compute from bone positions. */
                float dx = cx - px, dy = cy - py, dz = cz - pz;
                j->rest_length = sqrtf(dx*dx + dy*dy + dz*dz);
            }
            break;
        case 4: /* Lock (0-DOF rigid attachment). */
            j->type = PHYS_JOINT_LOCK;
            break;
        case 5: /* Copy rotation. */
            j->type = PHYS_JOINT_COPY_ROTATION;
            break;
        case 6: /* Limit rotation. */
            j->type = PHYS_JOINT_LIMIT_ROTATION;
            j->limit_min[0] = jd->limit_min[0];
            j->limit_min[1] = jd->limit_min[1];
            j->limit_min[2] = jd->limit_min[2];
            j->limit_max[0] = jd->limit_max[0];
            j->limit_max[1] = jd->limit_max[1];
            j->limit_max[2] = jd->limit_max[2];
            j->limit_axes = (uint8_t)jd->limit_axes;
            break;
        case 7: /* Limit position. */
            j->type = PHYS_JOINT_LIMIT_POSITION;
            j->limit_min[0] = jd->limit_min[0];
            j->limit_min[1] = jd->limit_min[1];
            j->limit_min[2] = jd->limit_min[2];
            j->limit_max[0] = jd->limit_max[0];
            j->limit_max[1] = jd->limit_max[1];
            j->limit_max[2] = jd->limit_max[2];
            j->limit_axes = (uint8_t)jd->limit_axes;
            break;
        case 8: /* Aim (track-to). */
            j->type = PHYS_JOINT_AIM;
            j->track_axis = (phys_vec3_t){
                jd->axis[0], jd->axis[1], jd->axis[2]
            };
            break;
        default:
            continue;  /* Unknown type — skip. */
        }

        count++;
    }

    return count;
}
