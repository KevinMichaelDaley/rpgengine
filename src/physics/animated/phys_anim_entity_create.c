/**
 * @file phys_anim_entity_create.c
 * @brief Create an animated entity: skeleton bones → physics world bodies.
 *
 * Creates bodies for bones that have colliders or participate in
 * physics-relevant constraints.  Bones without colliders or constraint
 * involvement are pruned (no body created).  Ghost bodies (no collider
 * but needed for constraint chains) get NO_BROADPHASE flag and inherit
 * averaged mass from their connected collider neighbors.
 *
 * Parent-child joints bridge over pruned bones by connecting each child
 * to its nearest ancestor that has a body.
 *
 * Non-static functions: 2 (phys_anim_entity_create, phys_anim_entity_destroy)
 */

#include "ferrum/physics/phys_anim_entity.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_overlap.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/animation/anim_constraint_rows.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

/**
 * @brief Check whether a constraint type has a physics-joint equivalent.
 *
 * Constraints like LIMIT_SCALE, COPY_SCALE, MAINTAIN_VOLUME etc. have
 * no rigid-body representation and should not cause ghost body creation.
 */
static bool constraint_is_physics_relevant_(constraint_type_t type) {
    switch (type) {
    case CONSTRAINT_IK:
    case CONSTRAINT_CHILD_OF:
    case CONSTRAINT_COPY_TRANSFORMS:
    case CONSTRAINT_COPY_ROTATION:
    case CONSTRAINT_COPY_LOCATION:
    case CONSTRAINT_DAMPED_TRACK:
    case CONSTRAINT_TRACK_TO:
    case CONSTRAINT_LOCKED_TRACK:
    case CONSTRAINT_LIMIT_ROTATION:
    case CONSTRAINT_LIMIT_LOCATION:
    case CONSTRAINT_FLOOR:
    case CONSTRAINT_PIVOT:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Build a bitmap of which bones need physics bodies.
 *
 * A bone needs a body if it:
 *   1. Has a collider (shape_type != NONE).
 *   2. Is an owner of a physics-relevant animation constraint.
 *   3. Is a target of a physics-relevant animation constraint.
 *   4. Is in an IK chain between end-effector and chain root.
 *   5. Sits on the parent chain between a body-bone and the root,
 *      ensuring the skeleton's body-bones form a connected tree.
 *
 * @param skel       Skeleton definition.
 * @param needs_body Output bitmap (n elements, caller-allocated).
 * @param n          Number of bones.
 */
static void build_needs_body_map_(const skeleton_def_t *skel,
                                  uint8_t *needs_body, uint32_t n) {
    memset(needs_body, 0, n);

    /* Mark bones with colliders. */
    if (skel->colliders) {
        for (uint32_t i = 0; i < n; i++) {
            if (skel->colliders[i].shape_type != BONE_COLLIDER_NONE) {
                needs_body[i] = 1;
            }
        }
    }

    /* Mark bones involved in physics-relevant constraints. */
    if (skel->constraints && skel->constraint_counts) {
        for (uint32_t bi = 0; bi < n; bi++) {
            uint32_t nc = skel->constraint_counts[bi];
            for (uint32_t ci = 0; ci < nc; ci++) {
                const constraint_def_t *def =
                    &skel->constraints[bi * skel->max_constraints_per_joint + ci];
                if (def->influence <= 0.0f) continue;
                if (!constraint_is_physics_relevant_(def->type)) continue;

                /* Owner needs a body. */
                needs_body[bi] = 1;

                /* Target needs a body. */
                if (def->target_bone_idx < n) {
                    needs_body[def->target_bone_idx] = 1;
                }

                /* IK chain: mark all bones from owner to chain root. */
                if (def->type == CONSTRAINT_IK) {
                    uint32_t chain_len = def->params.ik.chain_length;
                    if (chain_len == 0) chain_len = n;
                    uint32_t cur = bi;
                    for (uint32_t c = 0; c < chain_len; c++) {
                        needs_body[cur] = 1;
                        uint32_t par = skel->parent_indices[cur];
                        if (par == UINT32_MAX || par >= n) break;
                        cur = par;
                    }
                }
            }
        }
    }

    /* Connectivity pass: walk up from every body-bone to the root,
     * marking all intermediate ancestors as needing bodies.  This
     * ensures the skeleton's physics bodies form a single connected
     * tree — without this, non-collider branching bones (e.g. the
     * root) would disconnect subtrees into independent islands. */
    for (uint32_t i = 0; i < n; i++) {
        if (!needs_body[i]) continue;
        uint32_t cur = skel->parent_indices[i];
        while (cur != UINT32_MAX && cur < n) {
            if (needs_body[cur]) break; /* already connected */
            needs_body[cur] = 1;
            cur = skel->parent_indices[cur];
        }
    }
}

/**
 * @brief Find the nearest ancestor of bone_idx that has a body.
 *
 * Walks the parent chain from bone_idx's parent upward, returning
 * the first bone index whose body_indices entry is not UINT32_MAX.
 * Returns UINT32_MAX if no ancestor has a body (e.g., root bone).
 */
static uint32_t find_nearest_ancestor_body_(
    uint32_t bone_idx, const skeleton_def_t *skel,
    const uint32_t *body_indices) {
    uint32_t cur = skel->parent_indices[bone_idx];
    while (cur != UINT32_MAX && cur < skel->joint_count) {
        if (body_indices[cur] != UINT32_MAX) return cur;
        cur = skel->parent_indices[cur];
    }
    return UINT32_MAX;
}

/**
 * @brief Set up collider for a body based on bone_collider_desc_t.
 */
static void set_bone_collider_(phys_world_t *world, uint32_t body_idx,
                               const bone_collider_desc_t *col,
                               const skeleton_def_t *skel) {
    phys_vec3_t zero_offset = {0.0f, 0.0f, 0.0f};
    phys_quat_t identity = {0.0f, 0.0f, 0.0f, 1.0f};

    switch (col->shape_type) {
    case BONE_COLLIDER_CAPSULE: {
        float r = col->params[0] > 0.0f ? col->params[0] : 0.05f;
        float h = col->params[1] > 0.0f ? col->params[1] : 0.2f;
        int axis = (int)col->params[2];
        phys_quat_t rot = identity;
        if (axis == 0) {
            rot = (phys_quat_t){0.0f, 0.0f, 0.7071068f, 0.7071068f};
        } else if (axis == 2) {
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
    case BONE_COLLIDER_CONVEX_HULL: {
        if (!skel || !skel->hull_vertices || col->hull_count < 4) break;
        /* hull_offset is a vertex index (not byte offset); each vertex
         * is 3 floats (x,y,z).  Convert to phys_vec3_t pointer. */
        const float *verts = &skel->hull_vertices[col->hull_offset * 3];
        phys_world_set_convex_collider(
            world, body_idx,
            (const phys_vec3_t *)verts, col->hull_count,
            zero_offset, identity);
        break;
    }
    default:
        break;
    }
}

/**
 * @brief Average ghost body masses from their joint-connected neighbors.
 *
 * For each ghost body, scans all joints to find connected bodies.
 * Sets the ghost's mass (and inertia) to the average mass of connected
 * non-ghost bodies, ensuring smooth mass transitions through chains.
 */
static void average_ghost_masses_(phys_world_t *world,
                                  const uint32_t *body_indices,
                                  uint32_t bone_count) {
    phys_body_t *bodies = world->body_pool.bodies_curr;

    for (uint32_t i = 0; i < bone_count; i++) {
        uint32_t bi = body_indices[i];
        if (bi == UINT32_MAX) continue;
        if (!(bodies[bi].flags & PHYS_BODY_FLAG_NO_BROADPHASE)) continue;

        /* Find connected bodies via joints. */
        float mass_sum = 0.0f;
        uint32_t mass_count = 0;
        for (uint32_t ji = 0; ji < world->joint_count; ji++) {
            const phys_joint_t *j = &world->joints[ji];
            uint32_t other = UINT32_MAX;
            if (j->body_a == bi) other = j->body_b;
            else if (j->body_b == bi) other = j->body_a;
            if (other == UINT32_MAX) continue;

            float m = (bodies[other].inv_mass > 0.0f)
                        ? 1.0f / bodies[other].inv_mass : 0.0f;
            if (m > 0.0f) {
                mass_sum += m;
                mass_count++;
            }
        }

        float avg_mass = (mass_count > 0) ? (mass_sum / mass_count) : 1.0f;
        phys_body_set_mass(&bodies[bi], avg_mass);
        phys_body_set_sphere_inertia(&bodies[bi], avg_mass, 0.05f);
    }
}

/* ── Main entry point ─────────────────────────────────────────────── */

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

    /* ── Determine which bones need physics bodies ─────────────── */
    uint8_t *needs_body = (uint8_t *)calloc(n, 1);
    if (!needs_body) {
        phys_anim_entity_destroy(entity);
        return false;
    }
    build_needs_body_map_(skel, needs_body, n);

    /* ── Create bodies only for needed bones ───────────────────── */
    uint32_t body_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!needs_body[i]) {
            entity->body_indices[i] = UINT32_MAX;
            continue;
        }

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

        phys_body_t *body = phys_world_get_body(world, bi);
        if (!body) continue;

        body->position = (phys_vec3_t){
            world_pose[i].m[12],
            world_pose[i].m[13],
            world_pose[i].m[14]
        };
        body->orientation = quat_from_mat4(&world_pose[i]);

        if (!has_collider) {
            /* Ghost body: participates in joints only, solved via TGS
             * with graph coloring + sub-substepping for the skeleton
             * island.  Tier 0 ensures TGS solver mode for all ghost
             * body constraints.  Gravity enabled so ghosts fall with
             * the rest of the skeleton. */
            body->flags |= PHYS_BODY_FLAG_NO_BROADPHASE;
            body->tier = PHYS_TIER_0_DIRECT;
            phys_body_set_mass(body, 1.0f);
            phys_body_set_sphere_inertia(body, 1.0f, 0.05f);
            body->linear_damping  = 50.0f;
            body->angular_damping = 50.0f;
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
        case BONE_COLLIDER_CONVEX_HULL: {
            /* Approximate inertia as a box enclosing the hull AABB. */
            if (skel->hull_vertices && col->hull_count >= 4) {
                const float *hv = &skel->hull_vertices[col->hull_offset * 3];
                float minx = hv[0], maxx = hv[0];
                float miny = hv[1], maxy = hv[1];
                float minz = hv[2], maxz = hv[2];
                for (uint32_t vi = 1; vi < col->hull_count; vi++) {
                    float x = hv[vi*3+0], y = hv[vi*3+1], z = hv[vi*3+2];
                    if (x < minx) minx = x; if (x > maxx) maxx = x;
                    if (y < miny) miny = y; if (y > maxy) maxy = y;
                    if (z < minz) minz = z; if (z > maxz) maxz = z;
                }
                phys_vec3_t he = {
                    (maxx - minx) * 0.5f,
                    (maxy - miny) * 0.5f,
                    (maxz - minz) * 0.5f
                };
                if (he.x < 0.01f) he.x = 0.01f;
                if (he.y < 0.01f) he.y = 0.01f;
                if (he.z < 0.01f) he.z = 0.01f;
                phys_body_set_box_inertia(body, body_mass, he);
            } else {
                phys_body_set_box_inertia(body, body_mass,
                                           (phys_vec3_t){0.1f, 0.1f, 0.1f});
            }
            break;
        }
        default:
            phys_body_set_box_inertia(body, body_mass,
                                       (phys_vec3_t){0.1f, 0.5f, 0.1f});
            break;
        }

        set_bone_collider_(world, bi, col, skel);
        body->friction    = 0.5f;
        body->restitution = 0.0f;

        /* Compute estimated collider volume for drag scaling.
         * Cross-section ∝ volume^(2/3), so damping coeff scales with
         * that to approximate air resistance on differently-sized parts. */
        float volume = 0.001f; /* fallback: tiny cube */
        switch (col->shape_type) {
        case BONE_COLLIDER_SPHERE: {
            float r = col->params[0] > 0.0f ? col->params[0] : 0.1f;
            volume = (4.0f / 3.0f) * 3.14159f * r * r * r;
            break;
        }
        case BONE_COLLIDER_CAPSULE: {
            float r = col->params[0] > 0.0f ? col->params[0] : 0.05f;
            float h = col->params[1] > 0.0f ? col->params[1] : 0.2f;
            /* cylinder + 2 hemisphere caps = cylinder + sphere */
            volume = 3.14159f * r * r * h
                   + (4.0f / 3.0f) * 3.14159f * r * r * r;
            break;
        }
        case BONE_COLLIDER_BOX: {
            float hx = col->params[0] > 0.0f ? col->params[0] : 0.1f;
            float hy = col->params[1] > 0.0f ? col->params[1] : 0.1f;
            float hz = col->params[2] > 0.0f ? col->params[2] : 0.1f;
            volume = 8.0f * hx * hy * hz;
            break;
        }
        case BONE_COLLIDER_CONVEX_HULL: {
            /* Rough AABB volume estimate (already computed above). */
            if (skel->hull_vertices && col->hull_count >= 4) {
                const float *hv = &skel->hull_vertices[col->hull_offset * 3];
                float minx = hv[0], maxx = hv[0];
                float miny = hv[1], maxy = hv[1];
                float minz = hv[2], maxz = hv[2];
                for (uint32_t vi = 1; vi < col->hull_count; vi++) {
                    float x = hv[vi*3+0], y = hv[vi*3+1], z = hv[vi*3+2];
                    if (x < minx) minx = x; if (x > maxx) maxx = x;
                    if (y < miny) miny = y; if (y > maxy) maxy = y;
                    if (z < minz) minz = z; if (z > maxz) maxz = z;
                }
                /* ~52% fill factor (π/6) for convex shapes. */
                volume = (maxx-minx) * (maxy-miny) * (maxz-minz) * 0.52f;
            }
            break;
        }
        default:
            volume = 0.008f; /* 0.2^3 */
            break;
        }
        /* Approximate cross-section from volume for drag scaling.
         * Uses cbrt(vol/ref) for gentle scaling — larger parts get
         * proportionally more drag but not excessively. */
        float cross_scale = cbrtf(volume / 0.002f);
        if (cross_scale < 0.1f) cross_scale = 0.1f;
        body->linear_damping  = 50.0f * cross_scale;
        body->angular_damping = 50.0f * cross_scale;
    }

    entity->body_count = body_count;
    free(needs_body);

    /* ── Create joints from skeleton joint descriptors ──────────── */
    /* Parent-child joints now bridge over pruned bones: each child
     * connects to its nearest ancestor that has a body. */
    if (skel->joints && body_count > 1) {
        uint32_t max_joints = n * 2;  /* Extra room for constraint joints. */
        phys_joint_t *tmp_joints = (phys_joint_t *)calloc(
            max_joints, sizeof(phys_joint_t));
        if (tmp_joints) {
            /* Build parent-child joints with ancestor bridging. */
            uint32_t jc = 0;
            for (uint32_t i = 0; i < n && jc < max_joints; i++) {
                const bone_joint_desc_t *jd = &skel->joints[i];
                if (jd->joint_type == 0) continue;

                uint32_t body_child = entity->body_indices[i];
                if (body_child == UINT32_MAX) continue;

                /* Find nearest ancestor with a body (bridge over pruned). */
                uint32_t ancestor = find_nearest_ancestor_body_(
                    i, skel, entity->body_indices);
                if (ancestor == UINT32_MAX) continue;
                uint32_t body_parent = entity->body_indices[ancestor];

                phys_joint_t *j = &tmp_joints[jc];
                phys_joint_init(j);
                j->body_a = body_parent;
                j->body_b = body_child;

                /* Anchor at child bone position in each body's local space. */
                float cx = world_pose[i].m[12];
                float cy = world_pose[i].m[13];
                float cz = world_pose[i].m[14];
                float px = world_pose[ancestor].m[12];
                float py = world_pose[ancestor].m[13];
                float pz = world_pose[ancestor].m[14];

                phys_quat_t parent_orient = quat_from_mat4(&world_pose[ancestor]);
                phys_vec3_t world_delta = {cx - px, cy - py, cz - pz};
                j->local_anchor_a = quat_inv_rotate_vec3(
                    parent_orient, world_delta);
                j->local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};

                switch (jd->joint_type) {
                case 1: {
                    /* Ball joints become cone-twist with per-axis limits.
                     * If the skeleton provides limits, use them; otherwise
                     * apply generous default limits (±45° all axes). */
                    j->type = PHYS_JOINT_CONE_TWIST;
                    phys_quat_t child_orient = quat_from_mat4(&world_pose[i]);
                    j->rest_relative_orient = quat_mul(
                        child_orient, quat_conjugate(parent_orient));
                    if (jd->limit_axes != 0) {
                        j->limit_min[0] = jd->limit_min[0];
                        j->limit_min[1] = jd->limit_min[1];
                        j->limit_min[2] = jd->limit_min[2];
                        j->limit_max[0] = jd->limit_max[0];
                        j->limit_max[1] = jd->limit_max[1];
                        j->limit_max[2] = jd->limit_max[2];
                        j->limit_axes = (uint8_t)jd->limit_axes;
                    } else {
                        /* Default: ±45° on all axes. */
                        const float default_limit = 0.7854f; /* π/4 */
                        j->limit_min[0] = -default_limit;
                        j->limit_min[1] = -default_limit;
                        j->limit_min[2] = -default_limit;
                        j->limit_max[0] =  default_limit;
                        j->limit_max[1] =  default_limit;
                        j->limit_max[2] =  default_limit;
                        j->limit_axes = 7; /* All 3 axes. */
                    }
                    break;
                }
                case 2: {
                    j->type = PHYS_JOINT_HINGE;
                    phys_quat_t child_orient = quat_from_mat4(&world_pose[i]);
                    phys_vec3_t bone_axis = {
                        jd->axis[0], jd->axis[1], jd->axis[2]
                    };
                    phys_vec3_t world_axis = quat_rotate_vec3(
                        child_orient, bone_axis);
                    j->local_axis_a = quat_inv_rotate_vec3(
                        parent_orient, world_axis);
                    if (jd->limit_min[0] != 0.0f || jd->limit_max[0] != 0.0f) {
                        j->limit_min[0] = jd->limit_min[0];
                        j->limit_max[0] = jd->limit_max[0];
                        j->limit_axes = 1;
                    }
                    break;
                }
                case 3: {
                    j->type = PHYS_JOINT_DISTANCE;
                    if (jd->rest_length > 0.0f) {
                        j->rest_length = jd->rest_length;
                    } else {
                        float dx = cx-px, dy = cy-py, dz = cz-pz;
                        j->rest_length = sqrtf(dx*dx + dy*dy + dz*dz);
                    }
                    break;
                }
                case 4: j->type = PHYS_JOINT_LOCK; break;
                case 5: j->type = PHYS_JOINT_COPY_ROTATION; break;
                case 6:
                    j->type = PHYS_JOINT_LIMIT_ROTATION;
                    j->limit_min[0] = jd->limit_min[0];
                    j->limit_min[1] = jd->limit_min[1];
                    j->limit_min[2] = jd->limit_min[2];
                    j->limit_max[0] = jd->limit_max[0];
                    j->limit_max[1] = jd->limit_max[1];
                    j->limit_max[2] = jd->limit_max[2];
                    j->limit_axes = (uint8_t)jd->limit_axes;
                    break;
                case 7:
                    j->type = PHYS_JOINT_LIMIT_POSITION;
                    j->limit_min[0] = jd->limit_min[0];
                    j->limit_min[1] = jd->limit_min[1];
                    j->limit_min[2] = jd->limit_min[2];
                    j->limit_max[0] = jd->limit_max[0];
                    j->limit_max[1] = jd->limit_max[1];
                    j->limit_max[2] = jd->limit_max[2];
                    j->limit_axes = (uint8_t)jd->limit_axes;
                    break;
                case 8:
                    j->type = PHYS_JOINT_AIM;
                    j->track_axis = (phys_vec3_t){
                        jd->axis[0], jd->axis[1], jd->axis[2]
                    };
                    break;
                default: continue;
                }

                jc++;
            }

            /* Diagnostic: count joint types. */
            {
                uint32_t ct_count = 0, ball_count = 0, lock_count = 0, other_count = 0;
                for (uint32_t ji = 0; ji < jc; ji++) {
                    switch (tmp_joints[ji].type) {
                    case PHYS_JOINT_CONE_TWIST: ct_count++; break;
                    case PHYS_JOINT_BALL: ball_count++; break;
                    case PHYS_JOINT_LOCK: lock_count++; break;
                    default: other_count++; break;
                    }
                }
                fprintf(stderr, "  Joint types: cone_twist=%u ball=%u lock=%u other=%u\n",
                        ct_count, ball_count, lock_count, other_count);
            }

            /* Add distance-limit joints between parent-child ghost body
             * pairs.  These act like semi-collision constraints: inactive
             * within a slack band around the rest distance, but push/pull
             * when bodies drift too far apart or too close.  This prevents
             * the skeleton from collapsing when the cone-twist positional
             * rows can't fully converge within the iteration budget. */
            {
                const phys_body_t *bodies = world->body_pool.bodies_curr;
                uint32_t dl_count = 0;
                for (uint32_t i = 0; i < n && jc < max_joints; i++) {
                    const bone_joint_desc_t *jd = &skel->joints[i];
                    if (jd->joint_type == 0) continue;
                    uint32_t body_child = entity->body_indices[i];
                    if (body_child == UINT32_MAX) continue;
                    uint32_t ancestor = find_nearest_ancestor_body_(
                        i, skel, entity->body_indices);
                    if (ancestor == UINT32_MAX) continue;
                    uint32_t body_parent = entity->body_indices[ancestor];

                    /* Only add for ghost-to-ghost or ghost-to-collider. */
                    bool a_ghost = (bodies[body_parent].flags &
                                    PHYS_BODY_FLAG_NO_BROADPHASE) != 0;
                    bool b_ghost = (bodies[body_child].flags &
                                    PHYS_BODY_FLAG_NO_BROADPHASE) != 0;
                    if (!a_ghost && !b_ghost) continue;

                    /* Center-to-center rest distance. */
                    float px = world_pose[ancestor].m[12];
                    float py = world_pose[ancestor].m[13];
                    float pz = world_pose[ancestor].m[14];
                    float cx = world_pose[i].m[12];
                    float cy = world_pose[i].m[13];
                    float cz = world_pose[i].m[14];
                    float dx = cx - px, dy = cy - py, dz = cz - pz;
                    float rest = sqrtf(dx*dx + dy*dy + dz*dz);
                    if (rest < 1e-4f) continue;  /* Skip coincident bones. */

                    phys_joint_t *j = &tmp_joints[jc];
                    phys_joint_init(j);
                    j->type = PHYS_JOINT_DISTANCE;
                    j->body_a = body_parent;
                    j->body_b = body_child;
                    /* Anchors at body centers. */
                    j->local_anchor_a = (phys_vec3_t){0, 0, 0};
                    j->local_anchor_b = (phys_vec3_t){0, 0, 0};
                    /* Range mode: ±20% slack around rest distance. */
                    j->limit_axes = 1;
                    j->limit_min[0] = rest * 0.8f;
                    j->limit_max[0] = rest * 1.2f;
                    j->rest_length  = rest;
                    jc++;
                    dl_count++;
                }
                if (dl_count > 0) {
                    fprintf(stderr, "  Distance-limit joints: %u\n", dl_count);
                } else {
                    fprintf(stderr, "  Distance-limit joints: 0 (no ghost pairs found)\n");
                }
            }

            /* Add animation constraint joints (IK, aim, copy_rotation, etc.).
             * These get higher compliance since they represent animation
             * intentions, not rigid physical linkages. */
            if (skel->constraints && skel->constraint_counts) {
                uint32_t cjc = anim_constraints_to_joints(
                    skel, world_pose, entity->body_indices,
                    &tmp_joints[jc], max_joints - jc);
                for (uint32_t ci = 0; ci < cjc; ci++) {
                    tmp_joints[jc + ci].compliance = 1e-3f;
                }
                jc += cjc;
            }

            /* Set compliance on parent-child joints involving ghost bodies.
             * Ghost-to-ghost gets moderate slack; ghost-to-collider gets
             * a small amount of slack for stability. */
            {
                const phys_body_t *bodies = world->body_pool.bodies_curr;
                for (uint32_t ji = 0; ji < jc; ji++) {
                    phys_joint_t *j = &tmp_joints[ji];
                    if (j->compliance > 0.0f) continue; /* Already set (anim). */
                    bool a_ghost = (bodies[j->body_a].flags &
                                    PHYS_BODY_FLAG_NO_BROADPHASE) != 0;
                    bool b_ghost = (bodies[j->body_b].flags &
                                    PHYS_BODY_FLAG_NO_BROADPHASE) != 0;
                    if (a_ghost && b_ghost) {
                        j->compliance = 5e-4f;
                    } else if (a_ghost || b_ghost) {
                        j->compliance = 1e-4f;
                    }
                }
            }

            /* Register all joints in the world. */
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

    /* ── Average ghost body masses from connected neighbors ────── */
    average_ghost_masses_(world, entity->body_indices, n);

    /* ── Register collision exclusion pairs ──────────────────────── */

    /* 0. Exclude same collision-group body pairs. */
    if (skel->colliders) {
        for (uint32_t i = 0; i < n; i++) {
            uint32_t bi = entity->body_indices[i];
            if (bi == UINT32_MAX) continue;
            if (world->body_pool.bodies_curr[bi].flags &
                PHYS_BODY_FLAG_NO_BROADPHASE) continue;
            uint32_t gi = skel->colliders[i].collision_group;
            if (gi == 0) continue;
            for (uint32_t j = i + 1; j < n; j++) {
                uint32_t bj = entity->body_indices[j];
                if (bj == UINT32_MAX) continue;
                if (world->body_pool.bodies_curr[bj].flags &
                    PHYS_BODY_FLAG_NO_BROADPHASE) continue;
                if (skel->colliders[j].collision_group == gi) {
                    phys_world_exclude_pair(world, bi, bj);
                }
            }
        }
    }

    /* 1. Exclude all joint-connected body pairs. */
    for (uint32_t j = 0; j < world->joint_count; j++) {
        const phys_joint_t *jt = &world->joints[j];
        bool owns_a = false, owns_b = false;
        for (uint32_t i = 0; i < n && (!owns_a || !owns_b); i++) {
            if (entity->body_indices[i] == jt->body_a) owns_a = true;
            if (entity->body_indices[i] == jt->body_b) owns_b = true;
        }
        if (owns_a && owns_b) {
            phys_world_exclude_pair(world, jt->body_a, jt->body_b);
        }
    }

    /* 2. Exclude body pairs whose colliders overlap in the bind pose. */
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
        if (world->body_pool.bodies_curr[bi].flags &
            PHYS_BODY_FLAG_NO_BROADPHASE) continue;
        for (uint32_t j = i + 1; j < n; j++) {
            uint32_t bj = entity->body_indices[j];
            if (bj == UINT32_MAX) continue;
            if (world->body_pool.bodies_curr[bj].flags &
                PHYS_BODY_FLAG_NO_BROADPHASE) continue;
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
