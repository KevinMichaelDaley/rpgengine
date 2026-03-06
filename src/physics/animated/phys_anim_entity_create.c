/**
 * @file phys_anim_entity_create.c
 * @brief Create an animated entity: skeleton bones → physics world bodies.
 *
 * Creates one body per bone with a collider in the physics world,
 * sets collider shape + mass + flags, and creates joints between
 * parent-child pairs from the skeleton's bone_joint_desc_t array.
 * Auto-excludes collisions between joint-connected bones and bones
 * whose colliders overlap in the bind pose.
 *
 * Non-static functions: 2 (phys_anim_entity_create, phys_anim_entity_destroy)
 */

#include "ferrum/physics/phys_anim_entity.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_overlap.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/animation/anim_constraint_rows.h"
#include "ferrum/math/quat.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Set up collider for a body based on bone_collider_desc_t.
 *
 * Capsule axis is determined by the axis parameter (0=X, 1=Y, 2=Z).
 * Axis rotations are applied as a local rotation on the collider.
 */
static void set_bone_collider_(phys_world_t *world, uint32_t body_idx,
                               const bone_collider_desc_t *col) {
    phys_vec3_t zero_offset = {0.0f, 0.0f, 0.0f};
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};

    switch (col->shape_type) {
    case BONE_COLLIDER_CAPSULE: {
        float r = col->params[0] > 0.0f ? col->params[0] : 0.05f;
        float h = col->params[1] > 0.0f ? col->params[1] : 0.2f;
        /* Capsule half-height is half the cylinder segment.
         * params[2] = axis index (0=X, 1=Y, 2=Z).  Default Y-up. */
        int axis = (int)col->params[2];
        phys_quat_t rot = identity;
        if (axis == 0) {
            /* Rotate 90° around Z to align capsule along X. */
            rot = (phys_quat_t){0.0f, 0.0f, 0.7071068f, 0.7071068f};
        } else if (axis == 2) {
            /* Rotate 90° around X to align capsule along Z. */
            rot = (phys_quat_t){0.7071068f, 0.0f, 0.0f, 0.7071068f};
        }
        phys_world_set_capsule_collider(world, body_idx, r, h * 0.5f,
                                         zero_offset, rot);
        break;
    }
    case BONE_COLLIDER_BOX: {
        phys_vec3_t he = {
            col->params[0] > 0.0f ? col->params[0] : 0.1f,
            col->params[1] > 0.0f ? col->params[1] : 0.1f,
            col->params[2] > 0.0f ? col->params[2] : 0.1f
        };
        phys_world_set_box_collider(world, body_idx, he,
                                     zero_offset, identity);
        break;
    }
    case BONE_COLLIDER_SPHERE: {
        float r = col->params[0] > 0.0f ? col->params[0] : 0.1f;
        phys_world_set_sphere_collider(world, body_idx, r, zero_offset);
        break;
    }
    default:
        /* NONE or unsupported — no collider. */
        break;
    }
}

bool phys_anim_entity_create(phys_anim_entity_t *entity,
                             phys_world_t *world,
                             const skeleton_def_t *skel,
                             const mat4_t *world_pose) {
    if (!entity || !world || !skel || !world_pose || skel->joint_count == 0) {
        if (entity) memset(entity, 0, sizeof(*entity));
        return false;
    }

    memset(entity, 0, sizeof(*entity));
    uint32_t n = skel->joint_count;
    entity->bone_count = n;

    /* Allocate per-bone arrays. */
    entity->body_indices = (uint32_t *)malloc(n * sizeof(uint32_t));
    entity->bone_world   = (mat4_t *)malloc(n * sizeof(mat4_t));
    if (!entity->body_indices || !entity->bone_world) {
        phys_anim_entity_destroy(entity);
        return false;
    }

    /* Initialize bone_world from rest pose. */
    memcpy(entity->bone_world, world_pose, n * sizeof(mat4_t));

    /* Create bodies in the world for each bone.  Bones with colliders
     * get full physics bodies; bones without colliders get "ghost" bodies
     * (NO_BROADPHASE) that only participate in joint constraints. */
    uint32_t body_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        const bone_collider_desc_t *col = NULL;
        if (skel->colliders) col = &skel->colliders[i];

        bool has_collider = (col && col->shape_type != BONE_COLLIDER_NONE);

        uint32_t bi = phys_world_create_body(world);
        if (bi == UINT32_MAX) {
            entity->body_indices[i] = UINT32_MAX;
            continue;
        }

        entity->body_indices[i] = bi;
        body_count++;

        /* Set position and orientation from world pose. */
        phys_body_t *body = phys_world_get_body(world, bi);
        if (!body) continue;

        body->position = (phys_vec3_t){
            world_pose[i].m[12],
            world_pose[i].m[13],
            world_pose[i].m[14]
        };
        body->orientation = quat_from_mat4(&world_pose[i]);

        if (!has_collider) {
            /* Ghost body: participates in joints only. */
            body->flags |= PHYS_BODY_FLAG_NO_BROADPHASE;
            phys_body_set_mass(body, 0.5f);
            phys_body_set_sphere_inertia(body, 0.5f, 0.05f);
            continue;
        }

        /* Set mass, kinematic flag, and CCD. */
        float body_mass = (col->mass > 0.0f) ? col->mass : 1.0f;
        if (col->is_kinematic) {
            body->flags |= PHYS_BODY_FLAG_KINEMATIC;
            body->inv_mass = 0.0f;
        } else {
            phys_body_set_mass(body, body_mass);
        }

        if (col->ccd_enabled) {
            body->flags |= PHYS_BODY_FLAG_CCD;
        }

        /* Set shape-specific inertia. */
        switch (col->shape_type) {
        case BONE_COLLIDER_CAPSULE: {
            float r = col->params[0] > 0.0f ? col->params[0] : 0.05f;
            float h = col->params[1] > 0.0f ? col->params[1] : 0.2f;
            phys_body_set_capsule_inertia(body, body_mass, r, h * 0.5f);
            break;
        }
        case BONE_COLLIDER_BOX: {
            phys_vec3_t he = {
                col->params[0] > 0.0f ? col->params[0] : 0.1f,
                col->params[1] > 0.0f ? col->params[1] : 0.1f,
                col->params[2] > 0.0f ? col->params[2] : 0.1f
            };
            phys_body_set_box_inertia(body, body_mass, he);
            break;
        }
        case BONE_COLLIDER_SPHERE: {
            float r = col->params[0] > 0.0f ? col->params[0] : 0.1f;
            phys_body_set_sphere_inertia(body, body_mass, r);
            break;
        }
        default:
            phys_body_set_box_inertia(body, body_mass,
                                       (phys_vec3_t){0.1f, 0.5f, 0.1f});
            break;
        }

        /* Attach collider shape to the world. */
        set_bone_collider_(world, bi, col);

        /* Default material. */
        body->friction    = 0.5f;
        body->restitution = 0.0f;
    }

    entity->body_count = body_count;

    /* Create joints from skeleton joint descriptors. */
    if (skel->joints && body_count > 1) {
        /* Temporary buffer for joint building. */
        uint32_t max_joints = n;
        phys_joint_t *tmp_joints = (phys_joint_t *)calloc(
            max_joints, sizeof(phys_joint_t));
        if (tmp_joints) {
            uint32_t jc = anim_joint_descs_to_joints(
                skel, world_pose, entity->body_indices,
                tmp_joints, max_joints);

            /* Allocate joint ID tracking array. */
            if (jc > 0) {
                entity->joint_world_ids = (uint32_t *)malloc(
                    jc * sizeof(uint32_t));
            }

            uint32_t added = 0;
            for (uint32_t j = 0; j < jc; j++) {
                uint32_t jid = phys_world_add_joint(world, &tmp_joints[j]);
                if (jid != UINT32_MAX && entity->joint_world_ids) {
                    entity->joint_world_ids[added++] = jid;
                }
            }
            entity->joint_count = added;
            free(tmp_joints);
        }
    }

    /* ── Register collision exclusion pairs ──────────────────────── */

    /* 0. Exclude same collision-group body pairs.  Bones assigned to
     *    the same non-zero group (authored in Blender) never collide.
     *    Only applies to bodies with colliders (not ghost bodies). */
    if (skel->colliders) {
        for (uint32_t i = 0; i < n; i++) {
            uint32_t bi = entity->body_indices[i];
            if (bi == UINT32_MAX) continue;
            if (world->body_pool.bodies_curr[bi].flags & PHYS_BODY_FLAG_NO_BROADPHASE) continue;
            uint32_t gi = skel->colliders[i].collision_group;
            if (gi == 0) continue;  /* Group 0 = no filtering. */
            for (uint32_t j = i + 1; j < n; j++) {
                uint32_t bj = entity->body_indices[j];
                if (bj == UINT32_MAX) continue;
                if (world->body_pool.bodies_curr[bj].flags & PHYS_BODY_FLAG_NO_BROADPHASE) continue;
                if (skel->colliders[j].collision_group == gi) {
                    phys_world_exclude_pair(world, bi, bj);
                }
            }
        }
    }

    /* 1. Exclude all joint-connected body pairs. */
    for (uint32_t j = 0; j < world->joint_count; j++) {
        const phys_joint_t *jt = &world->joints[j];
        /* Only exclude joints that belong to this entity's bodies. */
        bool owns_a = false, owns_b = false;
        for (uint32_t i = 0; i < n && (!owns_a || !owns_b); i++) {
            if (entity->body_indices[i] == jt->body_a) owns_a = true;
            if (entity->body_indices[i] == jt->body_b) owns_b = true;
        }
        if (owns_a && owns_b) {
            phys_world_exclude_pair(world, jt->body_a, jt->body_b);
        }
    }

    /* 2. Exclude body pairs whose colliders overlap in the bind pose.
     *    This catches adjacent bones whose capsules naturally interpenetrate
     *    even without a direct joint connection.
     *    Only check pairs where both bodies have real colliders. */
    phys_overlap_ctx_t ovl_ctx = {
        .spheres      = world->spheres,
        .boxes        = world->boxes,
        .capsules     = world->capsules,
        .meshes       = world->meshes,
        .convex_hulls = world->convex_hulls,
        .halfspaces   = world->halfspaces,
        .compounds    = world->compounds,
    };

    for (uint32_t i = 0; i < n; i++) {
        uint32_t bi = entity->body_indices[i];
        if (bi == UINT32_MAX) continue;
        if (world->body_pool.bodies_curr[bi].flags & PHYS_BODY_FLAG_NO_BROADPHASE) continue;
        for (uint32_t j = i + 1; j < n; j++) {
            uint32_t bj = entity->body_indices[j];
            if (bj == UINT32_MAX) continue;
            if (world->body_pool.bodies_curr[bj].flags & PHYS_BODY_FLAG_NO_BROADPHASE) continue;

            /* Already excluded (e.g. by joint)? */
            if (phys_world_is_pair_excluded(world, bi, bj)) continue;

            const phys_collider_t *ci = &world->colliders[bi];
            const phys_collider_t *cj = &world->colliders[bj];

            phys_vec3_t pi = {
                world_pose[i].m[12], world_pose[i].m[13], world_pose[i].m[14]
            };
            phys_quat_t qi = quat_from_mat4(&world_pose[i]);

            phys_vec3_t pj = {
                world_pose[j].m[12], world_pose[j].m[13], world_pose[j].m[14]
            };
            phys_quat_t qj = quat_from_mat4(&world_pose[j]);

            if (phys_test_overlap(&ovl_ctx, ci, pi, qi, cj, pj, qj)) {
                phys_world_exclude_pair(world, bi, bj);
            }
        }
    }

    return true;
}

void phys_anim_entity_destroy(phys_anim_entity_t *entity) {
    if (!entity) return;
    free(entity->body_indices);
    free(entity->bone_world);
    free(entity->joint_world_ids);
    memset(entity, 0, sizeof(*entity));
}
