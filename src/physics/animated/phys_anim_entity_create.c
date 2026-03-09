/**
 * @file phys_anim_entity_create.c
 * @brief Create an animated entity: skeleton bones → physics world bodies.
 *
 * Creates bodies for bones that have colliders or participate in
 * physics-relevant constraints.  Bones without colliders or constraint
 * involvement are pruned (no body created).  Ghost bodies (no authored
 * collider but needed for constraint chains) get a small proxy sphere
 * so they still collide with the environment; joint-connected pairs are
 * excluded to prevent self-collision.  Ghost mass is averaged from
 * connected collider neighbors.
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

    /* Mark jointed bones and their direct parents as needing bodies.
     * Both the child bone (which has the joint) and its direct parent
     * must exist in the physics world (as ghosts if no collider). */
    if (skel->joints) {
        for (uint32_t i = 0; i < n; i++) {
            if (skel->joints[i].joint_type == 0) continue;
            needs_body[i] = 1;
            uint32_t par = skel->parent_indices[i];
            if (par != UINT32_MAX && par < n) {
                needs_body[par] = 1;
            }
        }
    }
}

/**
 * @brief Get the direct parent bone index.
 *
 * Returns the skeleton parent of bone_idx, or UINT32_MAX if root.
 */
static uint32_t direct_parent_bone_(
    uint32_t bone_idx, const skeleton_def_t *skel) {
    uint32_t par = skel->parent_indices[bone_idx];
    if (par == UINT32_MAX || par >= skel->joint_count) return UINT32_MAX;
    return par;
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
    case BONE_COLLIDER_POINT: {
        phys_world_set_point_collider(world, body_idx, zero_offset);
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
                                  uint32_t bone_count,
                                  const skeleton_def_t *skel) {
    (void)skel;
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
    /* Allocate head offsets: body-local offset from midpoint → head.
     * Only meaningful when tail_positions are available (bodies at
     * midpoints).  Used by sync to recover bone head position. */
    entity->head_offsets = (float *)calloc(n * 3, sizeof(float));
    if (!entity->body_indices || !entity->bone_world || !entity->head_offsets) {
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

        /* Position body at the midpoint between bone head and tail.
         * This gives both anchor_a and anchor_b non-zero offsets,
         * providing angular coupling for stable GS convergence.
         * Store the body-local offset from midpoint back to head
         * so sync can recover the bone head position for skinning. */
        float hx = world_pose[i].m[12];
        float hy = world_pose[i].m[13];
        float hz = world_pose[i].m[14];
        if (skel->tail_positions) {
            float tx = skel->tail_positions[i * 3 + 0];
            float ty = skel->tail_positions[i * 3 + 1];
            float tz = skel->tail_positions[i * 3 + 2];
            float mx = (hx + tx) * 0.5f;
            float my = (hy + ty) * 0.5f;
            float mz = (hz + tz) * 0.5f;
            body->position = (phys_vec3_t){ mx, my, mz };

            /* head_offset = head - midpoint in world space, then rotate
             * into body-local frame so it can be un-rotated at sync. */
            phys_vec3_t world_offset = { hx - mx, hy - my, hz - mz };
            phys_quat_t orient = quat_from_mat4(&world_pose[i]);
            phys_vec3_t local_off = quat_inv_rotate_vec3(orient, world_offset);
            entity->head_offsets[i * 3 + 0] = local_off.x;
            entity->head_offsets[i * 3 + 1] = local_off.y;
            entity->head_offsets[i * 3 + 2] = local_off.z;
        } else {
            body->position = (phys_vec3_t){hx, hy, hz};
        }
        body->orientation = quat_from_mat4(&world_pose[i]);

        if (!has_collider) {
            /* Ghost body: no authored collider.  Participates in joint
             * constraints (so IK and parent-child links still work) but
             * has no gravity and no broadphase.  High damping ensures
             * it settles quickly to wherever its joints place it
             * without injecting energy into the skeleton. */
            body->flags |= PHYS_BODY_FLAG_NO_BROADPHASE
                         | PHYS_BODY_FLAG_NO_GRAVITY;
            body->tier = PHYS_TIER_ANIM;
            phys_body_set_mass(body, 1.0f);
            phys_body_set_sphere_inertia(body, 1.0f, 0.05f);
            body->linear_damping  = 0.95f;
            body->angular_damping = 0.95f;
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
        /* Uniform damping for all ragdoll bodies.  Must be the same
         * across all connected bodies to prevent differential deceleration
         * during free fall (which causes anchor stretching).  The damping
         * formula is mass-independent: v' = v / (1 + c*dt), so equal
         * coefficients guarantee equal deceleration rates regardless
         * of body mass.  50 gives ~17% reduction per substep at dt=4.2ms,
         * providing strong post-impact settling. */
        (void)volume;
        body->linear_damping  = 50.0f;
        body->angular_damping = 50.0f;

        /* All animated entity bodies use the ANIM tier (XPBD solver). */
        body->tier = PHYS_TIER_ANIM;
    }

    entity->body_count = body_count;

    /* ── Ghost body mass averaging ─────────────────────────────── */
    /* Ghost bodies (no collider) inherit the average mass of their
     * immediate skeleton neighbors that DO have colliders/mass.
     * This prevents ghosts from being too light relative to their
     * connected collider bodies, which causes joint separation. */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bi = entity->body_indices[i];
        if (bi == UINT32_MAX) continue;

        const bone_collider_desc_t *col = NULL;
        if (skel->colliders) col = &skel->colliders[i];
        bool has_collider = (col && col->shape_type != BONE_COLLIDER_NONE);
        if (has_collider) continue; /* only fix ghosts */

        /* Gather masses from parent and children with bodies. */
        float mass_sum = 0.0f;
        uint32_t mass_count = 0;

        /* Parent. */
        uint32_t pi = skel->parent_indices[i];
        if (pi != UINT32_MAX && pi < n) {
            uint32_t pbi = entity->body_indices[pi];
            if (pbi != UINT32_MAX) {
                phys_body_t *pb = phys_world_get_body(world, pbi);
                if (pb && pb->inv_mass > 0.0f) {
                    mass_sum += 1.0f / pb->inv_mass;
                    mass_count++;
                }
            }
        }

        /* Children. */
        for (uint32_t c = 0; c < n; c++) {
            if (skel->parent_indices[c] != i) continue;
            uint32_t cbi = entity->body_indices[c];
            if (cbi == UINT32_MAX) continue;
            phys_body_t *cb = phys_world_get_body(world, cbi);
            if (cb && cb->inv_mass > 0.0f) {
                mass_sum += 1.0f / cb->inv_mass;
                mass_count++;
            }
        }

        float avg_mass = (mass_count > 0) ? (mass_sum / mass_count) : 1.0f;
        if (avg_mass < 0.1f) avg_mass = 0.1f;

        phys_body_t *body = phys_world_get_body(world, bi);
        if (body) {
            phys_body_set_mass(body, avg_mass);
            /* Approximate inertia as a small sphere of that mass. */
            phys_body_set_sphere_inertia(body, avg_mass, 0.05f);
        }
    }

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

                /* Connect to direct parent bone. */
                uint32_t parent_bone = direct_parent_bone_(i, skel);
                if (parent_bone == UINT32_MAX) continue;
                uint32_t body_parent = entity->body_indices[parent_bone];
                if (body_parent == UINT32_MAX) continue;

                phys_joint_t *j = &tmp_joints[jc];
                phys_joint_init(j);
                j->body_a = body_parent;
                j->body_b = body_child;

                /* Compute joint anchor positions.
                 * If the fskel provides explicit anchors (has_anchors),
                 * use those (armature-space points converted to body-local).
                 * Otherwise, default to child bone HEAD as the joint point. */
                phys_body_t *body_a = phys_world_get_body(world, body_parent);
                phys_body_t *body_b = phys_world_get_body(world, body_child);
                phys_quat_t parent_orient = body_a->orientation;
                phys_quat_t child_orient_q = body_b->orientation;

                if (jd->has_anchors) {
                    /* Exported anchors are in armature (engine) world space.
                     * Convert to body-local by subtracting body position
                     * and rotating into the body's local frame. */
                    phys_vec3_t anc_a_world = {
                        jd->anchor_a[0], jd->anchor_a[1], jd->anchor_a[2]
                    };
                    phys_vec3_t anc_b_world = {
                        jd->anchor_b[0], jd->anchor_b[1], jd->anchor_b[2]
                    };
                    phys_vec3_t da = {
                        anc_a_world.x - body_a->position.x,
                        anc_a_world.y - body_a->position.y,
                        anc_a_world.z - body_a->position.z
                    };
                    j->local_anchor_a = quat_inv_rotate_vec3(parent_orient, da);
                    phys_vec3_t db = {
                        anc_b_world.x - body_b->position.x,
                        anc_b_world.y - body_b->position.y,
                        anc_b_world.z - body_b->position.z
                    };
                    j->local_anchor_b = quat_inv_rotate_vec3(child_orient_q, db);
                } else {
                    /* Default: joint point = child bone HEAD. */
                    float jx = world_pose[i].m[12];
                    float jy = world_pose[i].m[13];
                    float jz = world_pose[i].m[14];
                    phys_vec3_t da = {
                        jx - body_a->position.x,
                        jy - body_a->position.y,
                        jz - body_a->position.z
                    };
                    j->local_anchor_a = quat_inv_rotate_vec3(parent_orient, da);
                    phys_vec3_t db = {
                        jx - body_b->position.x,
                        jy - body_b->position.y,
                        jz - body_b->position.z
                    };
                    j->local_anchor_b = quat_inv_rotate_vec3(child_orient_q, db);
                }

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
                        /* Auto rest length from body positions. */
                        float dx = body_a->position.x - body_b->position.x;
                        float dy = body_a->position.y - body_b->position.y;
                        float dz = body_a->position.z - body_b->position.z;
                        j->rest_length = sqrtf(dx*dx + dy*dy + dz*dz);
                    }
                    break;
                }
                case 4: {
                    /* Lock joints are remapped to cone-twist with tight
                     * limits.  Lock joints caused oscillation in the
                     * coupled solver due to zero compliance + zero damping.
                     * Tight cone-twist (±5°) approximates lock behavior. */
                    j->type = PHYS_JOINT_CONE_TWIST;
                    phys_quat_t child_orient = quat_from_mat4(&world_pose[i]);
                    j->rest_relative_orient = quat_mul(
                        child_orient, quat_conjugate(parent_orient));
                    const float tight = 0.0873f; /* ~5° */
                    j->limit_min[0] = -tight;
                    j->limit_min[1] = -tight;
                    j->limit_min[2] = -tight;
                    j->limit_max[0] =  tight;
                    j->limit_max[1] =  tight;
                    j->limit_max[2] =  tight;
                    j->limit_axes = 7; /* All 3 axes. */
                    break;
                }
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

                j->compliance = jd->compliance;
                j->angular_compliance = jd->angular_compliance;
                j->damping = jd->damping;
                j->yield_strength = jd->yield_strength;
                j->break_strength = jd->break_strength;

                /* Drive flags and compliance from Blender exporter. */
                j->flags = jd->drive_flags;
                j->drive_compliance = jd->drive_compliance;

                /* Temporary load-time diagnostic dump. */
                {
                    static const char *type_names[] = {
                        "DIST","BALL","HINGE","LOCK","CPROT",
                        "LROT","LPOS","AIM","IK","CTWIST"
                    };
                    const char *tn = (j->type < 10) ? type_names[j->type] : "?";
                    fprintf(stderr,
                        "  j%u: b%u->b%u %s lim_axes=%u "
                        "lim=[%.3f,%.3f,%.3f]->[%.3f,%.3f,%.3f] "
                        "damp=%.3f comp=%.6f ang_comp=%.6f rest_q=(%.4f,%.4f,%.4f,%.4f)\n",
                        jc, j->body_a, j->body_b, tn, j->limit_axes,
                        j->limit_min[0], j->limit_min[1], j->limit_min[2],
                        j->limit_max[0], j->limit_max[1], j->limit_max[2],
                        j->damping, j->compliance, j->angular_compliance,
                        j->rest_relative_orient.x,
                        j->rest_relative_orient.y,
                        j->rest_relative_orient.z,
                        j->rest_relative_orient.w);
                }

                jc++;
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
                    uint32_t parent_bone = direct_parent_bone_(i, skel);
                    if (parent_bone == UINT32_MAX) continue;
                    uint32_t body_parent = entity->body_indices[parent_bone];
                    if (body_parent == UINT32_MAX) continue;

                    /* Only add for ghost-to-ghost or ghost-to-collider. */
                    bool a_ghost = (bodies[body_parent].flags &
                                    PHYS_BODY_FLAG_NO_BROADPHASE) != 0;
                    bool b_ghost = (bodies[body_child].flags &
                                    PHYS_BODY_FLAG_NO_BROADPHASE) != 0;
                    if (!a_ghost && !b_ghost) continue;

                    /* Center-to-center rest distance using actual body
                     * midpoint positions, NOT bone head positions from
                     * world_pose.  Bodies are at bone midpoints so the
                     * distance constraint must match. */
                    float dx = bodies[body_child].position.x -
                               bodies[body_parent].position.x;
                    float dy = bodies[body_child].position.y -
                               bodies[body_parent].position.y;
                    float dz = bodies[body_child].position.z -
                               bodies[body_parent].position.z;
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
                (void)dl_count;
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
    average_ghost_masses_(world, entity->body_indices, n, skel);

    /* ── Register collision exclusion pairs ──────────────────────── */

    /* 0. Exclude same collision-group body pairs. */
    uint32_t group_excl_count = 0;
    if (skel->colliders) {
        for (uint32_t i = 0; i < n; i++) {
            uint32_t bi = entity->body_indices[i];
            if (bi == UINT32_MAX) continue;
            if (world->body_pool.bodies_curr[bi].flags &
                PHYS_BODY_FLAG_NO_BROADPHASE) continue;
            uint32_t gi = skel->colliders[i].collision_group;
            for (uint32_t j = i + 1; j < n; j++) {
                uint32_t bj = entity->body_indices[j];
                if (bj == UINT32_MAX) continue;
                if (world->body_pool.bodies_curr[bj].flags &
                    PHYS_BODY_FLAG_NO_BROADPHASE) continue;
                if (skel->colliders[j].collision_group == gi) {
                    phys_world_exclude_pair(world, bi, bj);
                    group_excl_count++;
                }
            }
        }
    }
    (void)group_excl_count;

    /* 1. Exclude all joint-connected body pairs. */
    uint32_t joint_excl_count = 0;
    for (uint32_t j = 0; j < world->joint_count; j++) {
        const phys_joint_t *jt = &world->joints[j];
        bool owns_a = false, owns_b = false;
        for (uint32_t i = 0; i < n && (!owns_a || !owns_b); i++) {
            if (entity->body_indices[i] == jt->body_a) owns_a = true;
            if (entity->body_indices[i] == jt->body_b) owns_b = true;
        }
        if (owns_a && owns_b) {
            phys_world_exclude_pair(world, jt->body_a, jt->body_b);
            joint_excl_count++;
        }
    }
    (void)joint_excl_count;

    /* 2. Exclude body pairs whose colliders overlap in the bind pose. */
    uint32_t overlap_excl_count = 0;
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
                overlap_excl_count++;
            }
        }
    }
    (void)overlap_excl_count;

    /* 3. Exclude near-adjacent bone pairs (within 2 hops in the joint
     * graph).  Adjacent bones are already excluded by step 1.  This
     * additionally excludes "grandparent–grandchild" and "sibling"
     * pairs that would fight the joint solver.  Non-adjacent pairs
     * (e.g. hand vs opposite thigh) are allowed to self-collide so
     * the ragdoll doesn't self-intersect.
     *
     * Build a per-bone parent index from the skeleton, then for each
     * pair check if they share a grandparent within 2 hops. */
    uint32_t near_excl_count = 0;
    {
        /* Build bone→parent mapping from the skeleton. */
        int32_t parent_of[256]; /* bone index → parent bone index, -1 = root */
        uint32_t max_bones = n < 256 ? n : 256;
        for (uint32_t i = 0; i < max_bones; i++) {
            uint32_t pi = skel->parent_indices[i];
            parent_of[i] = (pi < max_bones) ? (int32_t)pi : -1;
        }

        for (uint32_t i = 0; i < max_bones; i++) {
            uint32_t bi = entity->body_indices[i];
            if (bi == UINT32_MAX) continue;
            if (world->body_pool.bodies_curr[bi].flags &
                PHYS_BODY_FLAG_NO_BROADPHASE) continue;

            for (uint32_t j = i + 1; j < max_bones; j++) {
                uint32_t bj = entity->body_indices[j];
                if (bj == UINT32_MAX) continue;
                if (world->body_pool.bodies_curr[bj].flags &
                    PHYS_BODY_FLAG_NO_BROADPHASE) continue;
                if (phys_world_is_pair_excluded(world, bi, bj)) continue;

                /* Check if bones i and j are within 2 hops.
                 * Collect ancestors up to depth 2 for each bone. */
                int32_t anc_i[3] = { (int32_t)i, parent_of[i], -1 };
                if (anc_i[1] >= 0 && (uint32_t)anc_i[1] < max_bones)
                    anc_i[2] = parent_of[anc_i[1]];

                int32_t anc_j[3] = { (int32_t)j, parent_of[j], -1 };
                if (anc_j[1] >= 0 && (uint32_t)anc_j[1] < max_bones)
                    anc_j[2] = parent_of[anc_j[1]];

                bool near = false;
                for (int a = 0; a < 3 && !near; a++) {
                    if (anc_i[a] < 0) continue;
                    for (int b = 0; b < 3 && !near; b++) {
                        if (anc_j[b] < 0) continue;
                        if (anc_i[a] == anc_j[b]) near = true;
                    }
                }

                if (near) {
                    phys_world_exclude_pair(world, bi, bj);
                    near_excl_count++;
                }
            }
        }
    }
    (void)near_excl_count;

    return true;
}

void phys_anim_entity_destroy(phys_anim_entity_t *entity) {
    if (!entity) return;
    free(entity->body_indices);
    free(entity->bone_world);
    free(entity->joint_world_ids);
    free(entity->head_offsets);
    memset(entity, 0, sizeof(*entity));
}
