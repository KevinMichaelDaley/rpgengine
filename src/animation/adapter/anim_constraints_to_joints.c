/**
 * @file anim_constraints_to_joints.c
 * @brief Map animation constraint_def_t entries to physics joints.
 *
 * Iterates a skeleton's constraint definitions and produces
 * phys_joint_t entries for each constraint that has a physics
 * joint equivalent.  Body indices use bone_to_body_map to translate
 * bone indices to body pool slots.
 *
 * Constraint → Joint mapping:
 *   Copy Location   → PHYS_JOINT_BALL  (3 pos rows)
 *   Copy Rotation   → PHYS_JOINT_COPY_ROTATION (3 ang rows)
 *   Copy Transforms → PHYS_JOINT_LOCK  (6 rows)
 *   Child Of        → PHYS_JOINT_LOCK  (6 rows)
 *   IK              → PHYS_JOINT_BALL  (end-effector → target)
 *   Damped Track    → PHYS_JOINT_AIM   (2 ang rows)
 *   Track To        → PHYS_JOINT_AIM   (2 ang rows)
 *   Locked Track    → PHYS_JOINT_AIM   (2 ang rows)
 *   Limit Rotation  → PHYS_JOINT_LIMIT_ROTATION (up to 3 clamped)
 *   Limit Location  → PHYS_JOINT_LIMIT_POSITION (up to 3 clamped)
 *   Floor           → PHYS_JOINT_LIMIT_POSITION (Y min, 1 row)
 *   Pivot           → PHYS_JOINT_BALL  (at pivot anchor)
 *
 * Unsupported (no rigid-body equivalent):
 *   Copy Scale, Limit Scale, Maintain Volume, Shrinkwrap,
 *   Spline IK, Action, Clamp To, Transformation.
 *
 * Non-static functions: 1 (anim_constraints_to_joints)
 */

#include "ferrum/animation/anim_constraint_rows.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/physics/joint.h"
#include "ferrum/math/quat.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief Set up a ball joint between two bones (owner and target).
 *
 * The anchor is placed at the owner bone's world position, expressed
 * in each body's local frame.  This way both anchors coincide in
 * world space at setup time (zero initial error).
 */
static void setup_ball(phys_joint_t *j,
                       uint32_t body_owner, uint32_t body_target,
                       float influence,
                       const mat4_t *world_pose,
                       uint32_t owner_bone, uint32_t target_bone) {
    phys_joint_init(j);
    j->type = PHYS_JOINT_BALL;
    j->body_a = body_target;
    j->body_b = body_owner;
    j->spring_stiffness = influence;

    /* Anchor at the owner bone's world position. */
    phys_vec3_t owner_pos = {
        world_pose[owner_bone].m[12],
        world_pose[owner_bone].m[13],
        world_pose[owner_bone].m[14]
    };
    phys_vec3_t target_pos = {
        world_pose[target_bone].m[12],
        world_pose[target_bone].m[13],
        world_pose[target_bone].m[14]
    };

    /* local_anchor_a (on target body): owner_pos in target's local frame. */
    phys_quat_t target_orient = quat_from_mat4(&world_pose[target_bone]);
    phys_vec3_t delta_a = {
        owner_pos.x - target_pos.x,
        owner_pos.y - target_pos.y,
        owner_pos.z - target_pos.z
    };
    j->local_anchor_a = quat_inv_rotate_vec3(target_orient, delta_a);

    /* local_anchor_b (on owner body): at body origin since body center
     * is at the owner bone position. */
    j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Set up a lock joint between two bones.
 *
 * Anchors are placed at the owner bone's world position so both
 * world-space anchors coincide initially.
 */
static void setup_lock(phys_joint_t *j,
                       uint32_t body_owner, uint32_t body_target,
                       float influence,
                       const mat4_t *world_pose,
                       uint32_t owner_bone, uint32_t target_bone) {
    phys_joint_init(j);
    j->type = PHYS_JOINT_LOCK;
    j->body_a = body_target;
    j->body_b = body_owner;
    j->spring_stiffness = influence;

    phys_vec3_t owner_pos = {
        world_pose[owner_bone].m[12],
        world_pose[owner_bone].m[13],
        world_pose[owner_bone].m[14]
    };
    phys_vec3_t target_pos = {
        world_pose[target_bone].m[12],
        world_pose[target_bone].m[13],
        world_pose[target_bone].m[14]
    };
    phys_quat_t target_orient = quat_from_mat4(&world_pose[target_bone]);
    phys_vec3_t delta_a = {
        owner_pos.x - target_pos.x,
        owner_pos.y - target_pos.y,
        owner_pos.z - target_pos.z
    };
    j->local_anchor_a = quat_inv_rotate_vec3(target_orient, delta_a);
    j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Set up a copy-rotation joint between two bones.
 *
 * Copy-rotation is angular-only — anchors placed at owner bone
 * position for any positional rows the solver might add.
 */
static void setup_copy_rot(phys_joint_t *j,
                           uint32_t body_owner, uint32_t body_target,
                           float influence,
                           const mat4_t *world_pose,
                           uint32_t owner_bone, uint32_t target_bone) {
    phys_joint_init(j);
    j->type = PHYS_JOINT_COPY_ROTATION;
    j->body_a = body_target;
    j->body_b = body_owner;
    j->spring_stiffness = influence;

    phys_vec3_t owner_pos = {
        world_pose[owner_bone].m[12],
        world_pose[owner_bone].m[13],
        world_pose[owner_bone].m[14]
    };
    phys_vec3_t target_pos = {
        world_pose[target_bone].m[12],
        world_pose[target_bone].m[13],
        world_pose[target_bone].m[14]
    };
    phys_quat_t target_orient = quat_from_mat4(&world_pose[target_bone]);
    phys_vec3_t delta_a = {
        owner_pos.x - target_pos.x,
        owner_pos.y - target_pos.y,
        owner_pos.z - target_pos.z
    };
    j->local_anchor_a = quat_inv_rotate_vec3(target_orient, delta_a);
    j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Set up an aim joint (Damped Track / Track To).
 */
static void setup_aim(phys_joint_t *j,
                      uint32_t body_owner, uint32_t body_target,
                      const constraint_def_t *def,
                      const mat4_t *world_pose,
                      uint32_t owner_bone, uint32_t target_bone) {
    phys_joint_init(j);
    j->type = PHYS_JOINT_AIM;
    j->body_a = body_target;
    j->body_b = body_owner;
    j->spring_stiffness = def->influence;

    /* Anchors at owner position. */
    phys_vec3_t owner_pos = {
        world_pose[owner_bone].m[12],
        world_pose[owner_bone].m[13],
        world_pose[owner_bone].m[14]
    };
    phys_vec3_t target_pos = {
        world_pose[target_bone].m[12],
        world_pose[target_bone].m[13],
        world_pose[target_bone].m[14]
    };
    phys_quat_t target_orient = quat_from_mat4(&world_pose[target_bone]);
    phys_vec3_t delta_a = {
        owner_pos.x - target_pos.x,
        owner_pos.y - target_pos.y,
        owner_pos.z - target_pos.z
    };
    j->local_anchor_a = quat_inv_rotate_vec3(target_orient, delta_a);
    j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    /* Determine track axis from constraint params. */
    if (def->type == CONSTRAINT_DAMPED_TRACK) {
        switch (def->params.damped_track.track_axis) {
        case 0: j->track_axis = (phys_vec3_t){ 1, 0, 0}; break;
        case 1: j->track_axis = (phys_vec3_t){-1, 0, 0}; break;
        case 2: j->track_axis = (phys_vec3_t){ 0, 1, 0}; break;
        case 3: j->track_axis = (phys_vec3_t){ 0,-1, 0}; break;
        case 4: j->track_axis = (phys_vec3_t){ 0, 0, 1}; break;
        case 5: j->track_axis = (phys_vec3_t){ 0, 0,-1}; break;
        default: j->track_axis = (phys_vec3_t){ 0, 1, 0}; break;
        }
    } else {
        /* Track To / Locked Track: default +Y. */
        j->track_axis = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    }
}

uint32_t anim_constraints_to_joints(
    const skeleton_def_t *skel,
    const mat4_t *world_pose,
    const uint32_t *bone_to_body_map,
    phys_joint_t *out_joints,
    uint32_t max_joints) {
    if (!skel || !world_pose || !bone_to_body_map || !out_joints) return 0;

    uint32_t count = 0;
    uint32_t n = skel->joint_count;

    for (uint32_t bi = 0; bi < n && count < max_joints; bi++) {
        uint32_t nc = skel->constraint_counts[bi];
        if (nc == 0) continue;

        uint32_t body_owner = bone_to_body_map[bi];
        if (body_owner == UINT32_MAX) continue;

        for (uint32_t ci = 0; ci < nc && count < max_joints; ci++) {
            const constraint_def_t *def =
                &skel->constraints[bi * skel->max_constraints_per_joint + ci];
            if (def->influence <= 0.0f) continue;

            /* Resolve target body index. */
            uint32_t body_target = UINT32_MAX;
            if (def->target_bone_idx != UINT32_MAX &&
                def->target_bone_idx < n) {
                body_target = bone_to_body_map[def->target_bone_idx];
            }

            switch (def->type) {
            case CONSTRAINT_COPY_LOCATION:
                if (body_target == UINT32_MAX) break;
                setup_ball(&out_joints[count], body_owner, body_target,
                           def->influence, world_pose,
                           bi, def->target_bone_idx);
                count++;
                break;

            case CONSTRAINT_COPY_ROTATION:
                if (body_target == UINT32_MAX) break;
                setup_copy_rot(&out_joints[count], body_owner, body_target,
                               def->influence, world_pose,
                               bi, def->target_bone_idx);
                count++;
                break;

            case CONSTRAINT_COPY_TRANSFORMS:
            case CONSTRAINT_CHILD_OF:
                if (body_target == UINT32_MAX) break;
                setup_lock(&out_joints[count], body_owner, body_target,
                           def->influence, world_pose,
                           bi, def->target_bone_idx);
                count++;
                break;

            case CONSTRAINT_IK: {
                if (body_target == UINT32_MAX) break;
                /* IK target world position. */
                uint32_t target_bone = def->target_bone_idx;
                phys_vec3_t target_pos = {
                    world_pose[target_bone].m[12],
                    world_pose[target_bone].m[13],
                    world_pose[target_bone].m[14]
                };

                /* Walk the chain from the IK bone (end-effector) toward
                 * the root, creating one IK joint per consecutive pair.
                 * chain_length=0 means "entire chain to root". */
                uint32_t chain_len = def->params.ik.chain_length;
                if (chain_len == 0) chain_len = n;

                /* End-effector body is the body owning this constraint. */
                uint32_t ee_body = body_owner;

                uint32_t cur = bi;
                for (uint32_t ci2 = 0; ci2 < chain_len && count < max_joints;
                     ci2++) {
                    uint32_t par = skel->parent_indices[cur];
                    if (par == UINT32_MAX || par >= n) break;

                    uint32_t body_cur = bone_to_body_map[cur];
                    uint32_t body_par = bone_to_body_map[par];
                    if (body_cur == UINT32_MAX || body_par == UINT32_MAX) {
                        cur = par;
                        continue;
                    }

                    phys_joint_t *j = &out_joints[count];
                    phys_joint_init(j);
                    j->type = PHYS_JOINT_IK;
                    j->body_a = body_par;
                    j->body_b = body_cur;
                    j->spring_stiffness = def->influence;
                    j->damping = 5.0f;
                    j->ik_ee_body = ee_body;
                    j->ik_target_body = body_target;
                    j->ik_target_pos = target_pos;

                    /* Anchor at cur bone position in each body's local
                     * frame so world anchors coincide at rest. */
                    phys_vec3_t cur_pos = {
                        world_pose[cur].m[12],
                        world_pose[cur].m[13],
                        world_pose[cur].m[14]
                    };
                    phys_vec3_t par_pos = {
                        world_pose[par].m[12],
                        world_pose[par].m[13],
                        world_pose[par].m[14]
                    };
                    phys_quat_t par_orient = quat_from_mat4(&world_pose[par]);
                    phys_vec3_t delta = {
                        cur_pos.x - par_pos.x,
                        cur_pos.y - par_pos.y,
                        cur_pos.z - par_pos.z
                    };
                    j->local_anchor_a = quat_inv_rotate_vec3(par_orient, delta);
                    j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

                    count++;

                    cur = par;
                }
                break;
            }

            case CONSTRAINT_DAMPED_TRACK:
            case CONSTRAINT_TRACK_TO:
            case CONSTRAINT_LOCKED_TRACK:
                if (body_target == UINT32_MAX) break;
                setup_aim(&out_joints[count], body_owner, body_target, def,
                          world_pose, bi, def->target_bone_idx);
                count++;
                break;

            case CONSTRAINT_LIMIT_ROTATION: {
                phys_joint_t *j = &out_joints[count];
                phys_joint_init(j);
                j->type = PHYS_JOINT_LIMIT_ROTATION;
                /* For limit constraints, body_a is the reference frame.
                 * Use parent bone if available, else self. */
                uint32_t parent_idx = skel->parent_indices[bi];
                uint32_t body_ref = UINT32_MAX;
                uint32_t ref_bone = bi;
                if (parent_idx != UINT32_MAX && parent_idx < n) {
                    body_ref = bone_to_body_map[parent_idx];
                    ref_bone = parent_idx;
                }
                if (body_ref == UINT32_MAX) { body_ref = body_owner; ref_bone = bi; }
                j->body_a = body_ref;
                j->body_b = body_owner;
                j->spring_stiffness = def->influence;

                /* Anchor at owner bone position. */
                if (body_ref != body_owner) {
                    phys_vec3_t own_pos = {
                        world_pose[bi].m[12], world_pose[bi].m[13], world_pose[bi].m[14]
                    };
                    phys_vec3_t ref_pos = {
                        world_pose[ref_bone].m[12], world_pose[ref_bone].m[13], world_pose[ref_bone].m[14]
                    };
                    phys_quat_t ref_orient = quat_from_mat4(&world_pose[ref_bone]);
                    phys_vec3_t delta = {own_pos.x - ref_pos.x, own_pos.y - ref_pos.y, own_pos.z - ref_pos.z};
                    j->local_anchor_a = quat_inv_rotate_vec3(ref_orient, delta);
                }

                j->limit_axes = 0;
                if (def->params.limit_rotation.use_limit_x) {
                    j->limit_axes |= (1u << 0);
                    j->limit_min[0] = def->params.limit_rotation.min_x;
                    j->limit_max[0] = def->params.limit_rotation.max_x;
                }
                if (def->params.limit_rotation.use_limit_y) {
                    j->limit_axes |= (1u << 1);
                    j->limit_min[1] = def->params.limit_rotation.min_y;
                    j->limit_max[1] = def->params.limit_rotation.max_y;
                }
                if (def->params.limit_rotation.use_limit_z) {
                    j->limit_axes |= (1u << 2);
                    j->limit_min[2] = def->params.limit_rotation.min_z;
                    j->limit_max[2] = def->params.limit_rotation.max_z;
                }
                if (j->limit_axes) count++;
                break;
            }

            case CONSTRAINT_LIMIT_LOCATION: {
                phys_joint_t *j = &out_joints[count];
                phys_joint_init(j);
                j->type = PHYS_JOINT_LIMIT_POSITION;
                uint32_t parent_idx2 = skel->parent_indices[bi];
                uint32_t body_ref2 = UINT32_MAX;
                uint32_t ref_bone2 = bi;
                if (parent_idx2 != UINT32_MAX && parent_idx2 < n) {
                    body_ref2 = bone_to_body_map[parent_idx2];
                    ref_bone2 = parent_idx2;
                }
                if (body_ref2 == UINT32_MAX) { body_ref2 = body_owner; ref_bone2 = bi; }
                j->body_a = body_ref2;
                j->body_b = body_owner;
                j->spring_stiffness = def->influence;

                /* Anchor at owner bone position. */
                if (body_ref2 != body_owner) {
                    phys_vec3_t own_pos = {
                        world_pose[bi].m[12], world_pose[bi].m[13], world_pose[bi].m[14]
                    };
                    phys_vec3_t ref_pos = {
                        world_pose[ref_bone2].m[12], world_pose[ref_bone2].m[13], world_pose[ref_bone2].m[14]
                    };
                    phys_quat_t ref_orient = quat_from_mat4(&world_pose[ref_bone2]);
                    phys_vec3_t delta = {own_pos.x - ref_pos.x, own_pos.y - ref_pos.y, own_pos.z - ref_pos.z};
                    j->local_anchor_a = quat_inv_rotate_vec3(ref_orient, delta);
                }

                j->limit_axes = 0;
                /* Default limits: ±infinity (inactive). */
                for (int ax = 0; ax < 3; ax++) {
                    j->limit_min[ax] = -1e10f;
                    j->limit_max[ax] =  1e10f;
                }
                if (def->params.limit_location.use_min_x) {
                    j->limit_axes |= (1u << 0);
                    j->limit_min[0] = def->params.limit_location.min_x;
                }
                if (def->params.limit_location.use_max_x) {
                    j->limit_axes |= (1u << 0);
                    j->limit_max[0] = def->params.limit_location.max_x;
                }
                if (def->params.limit_location.use_min_y) {
                    j->limit_axes |= (1u << 1);
                    j->limit_min[1] = def->params.limit_location.min_y;
                }
                if (def->params.limit_location.use_max_y) {
                    j->limit_axes |= (1u << 1);
                    j->limit_max[1] = def->params.limit_location.max_y;
                }
                if (def->params.limit_location.use_min_z) {
                    j->limit_axes |= (1u << 2);
                    j->limit_min[2] = def->params.limit_location.min_z;
                }
                if (def->params.limit_location.use_max_z) {
                    j->limit_axes |= (1u << 2);
                    j->limit_max[2] = def->params.limit_location.max_z;
                }
                if (j->limit_axes) count++;
                break;
            }

            case CONSTRAINT_FLOOR: {
                /* Floor = one-sided position limit on Y axis. */
                phys_joint_t *j = &out_joints[count];
                phys_joint_init(j);
                j->type = PHYS_JOINT_LIMIT_POSITION;
                /* body_a provides reference frame (target or self). */
                j->body_a = (body_target != UINT32_MAX) ? body_target
                                                         : body_owner;
                j->body_b = body_owner;
                j->spring_stiffness = def->influence;
                j->limit_axes = (1u << 1);  /* Y axis only. */
                j->limit_min[1] = def->params.floor.offset;
                j->limit_max[1] = 1e10f;   /* No upper limit. */
                count++;
                break;
            }

            case CONSTRAINT_PIVOT:
                if (body_target == UINT32_MAX) break;
                setup_ball(&out_joints[count], body_owner, body_target,
                           def->influence, world_pose,
                           bi, def->target_bone_idx);
                count++;
                break;

            /* Unsupported constraints (no rigid-body equivalent). */
            case CONSTRAINT_COPY_SCALE:
            case CONSTRAINT_LIMIT_SCALE:
            case CONSTRAINT_MAINTAIN_VOLUME:
            case CONSTRAINT_SHRINKWRAP:
            case CONSTRAINT_SPLINE_IK:
            case CONSTRAINT_ACTION:
            case CONSTRAINT_CLAMP_TO:
            case CONSTRAINT_TRANSFORMATION:
            default:
                break;
            }
        }
    }

    return count;
}
