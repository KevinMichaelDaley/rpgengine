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

        /* Anchor at child bone position, in each body's local space. */
        float cx = world_pose[i].m[12];
        float cy = world_pose[i].m[13];
        float cz = world_pose[i].m[14];
        float px = world_pose[parent].m[12];
        float py = world_pose[parent].m[13];
        float pz = world_pose[parent].m[14];

        j->local_anchor_a = (phys_vec3_t){cx - px, cy - py, cz - pz};
        j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

        switch (jd->joint_type) {
        case 1: /* Ball joint. */
            j->type = PHYS_JOINT_BALL;
            break;
        case 2: /* Hinge joint. */
            j->type = PHYS_JOINT_HINGE;
            j->local_axis_a = (phys_vec3_t){
                jd->axis[0], jd->axis[1], jd->axis[2]
            };
            /* Apply angular limits if specified. */
            if (jd->limit_min != 0.0f || jd->limit_max != 0.0f) {
                /* Hinge limits are enforced via the hinge builder's
                 * angular rows.  Store limits for future use. */
                j->limit_min[0] = jd->limit_min;
                j->limit_max[0] = jd->limit_max;
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
        default:
            continue;  /* Unknown type — skip. */
        }

        count++;
    }

    return count;
}
