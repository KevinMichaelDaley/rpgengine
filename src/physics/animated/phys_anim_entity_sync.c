/**
 * @file phys_anim_entity_sync.c
 * @brief Sync animated entity bone transforms from/to physics world.
 *
 * Reads body positions + orientations from the world and writes them
 * as bone mat4_t transforms.  Bones without bodies propagate from
 * their parent's world transform via rest-local multiplication.
 * Also provides push_kinematic() to write animation-driven bone
 * poses into kinematic bodies.
 *
 * Non-static functions: 2 (phys_anim_entity_sync_from_world,
 *                          phys_anim_entity_push_kinematic)
 */

#include "ferrum/physics/phys_anim_entity.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

#include <stddef.h>

void phys_anim_entity_sync_from_world(phys_anim_entity_t *entity,
                                      const phys_world_t *world,
                                      const skeleton_def_t *skel) {
    if (!entity || !world) return;

    /* Pass 1: update bones that have physics bodies.
     * Body position is at bone midpoint.  Recover bone HEAD
     * position by adding the rotated head_offset. */
    for (uint32_t i = 0; i < entity->bone_count; i++) {
        uint32_t bi = entity->body_indices[i];
        if (bi == UINT32_MAX) continue;

        const phys_body_t *body = phys_world_get_body(
            (phys_world_t *)world, bi);
        if (!body) continue;

        /* Build rotation matrix from quaternion. */
        mat4_t rot;
        quat_to_mat4(body->orientation, &rot);

        /* Recover bone head position from body midpoint + head offset. */
        float tx = body->position.x;
        float ty = body->position.y;
        float tz = body->position.z;
        if (entity->head_offsets) {
            phys_vec3_t local_off = {
                entity->head_offsets[i * 3 + 0],
                entity->head_offsets[i * 3 + 1],
                entity->head_offsets[i * 3 + 2]
            };
            phys_vec3_t world_off = quat_rotate_vec3(body->orientation,
                                                     local_off);
            tx += world_off.x;
            ty += world_off.y;
            tz += world_off.z;
        }
        rot.m[12] = tx;
        rot.m[13] = ty;
        rot.m[14] = tz;

        entity->bone_world[i] = rot;
    }

    /* Pass 2: propagate non-body bones.
     *
     * Two sub-passes:
     *   2a (reverse): "pull up" — rootward non-body bones with no
     *       parent body derive their world transform from their first
     *       child that has a body:
     *           bone_world[i] = child_world * inverse(rest_local[child])
     *       Reverse order ensures children are resolved before parents.
     *
     *   2b (forward): "push down" — remaining non-body bones (mid-chain)
     *       inherit from their parent:
     *           bone_world[i] = parent_world * rest_local[i]
     *       Forward order ensures parents are resolved before children.
     */
    if (skel && skel->parent_indices && skel->rest_local) {
        /* 2a: pull-up for rootward non-body bones. */
        for (uint32_t i = entity->bone_count; i-- > 0; ) {
            if (entity->body_indices[i] != UINT32_MAX) continue;

            /* Skip if this bone has a parent that already has a body
             * — it will be handled by the forward pass. */
            uint32_t pi = skel->parent_indices[i];
            if (pi != UINT32_MAX && pi < entity->bone_count &&
                entity->body_indices[pi] != UINT32_MAX) {
                continue;
            }

            /* Find first child with a body (or already resolved). */
            for (uint32_t c = i + 1; c < entity->bone_count; c++) {
                if (skel->parent_indices[c] != i) continue;
                if (entity->body_indices[c] == UINT32_MAX) continue;

                /* bone_world[i] = child_world * inv(rest_local[child]) */
                mat4_t inv_local;
                if (mat4_inverse(skel->rest_local[c], &inv_local) == 0) {
                    entity->bone_world[i] = mat4_mul(
                        entity->bone_world[c], inv_local);
                }
                break;
            }
        }

        /* 2b: push-down for mid-chain non-body bones. */
        for (uint32_t i = 0; i < entity->bone_count; i++) {
            if (entity->body_indices[i] != UINT32_MAX) continue;

            uint32_t par = skel->parent_indices[i];
            if (par == UINT32_MAX || par >= entity->bone_count) continue;

            /* bone_world[i] = parent_world * rest_local[i] */
            entity->bone_world[i] = mat4_mul(entity->bone_world[par],
                                             skel->rest_local[i]);
        }
    }
}

void phys_anim_entity_push_kinematic(phys_anim_entity_t *entity,
                                     phys_world_t *world,
                                     const mat4_t *world_pose,
                                     uint32_t count) {
    if (!entity || !world || !world_pose) return;

    uint32_t n = count < entity->bone_count ? count : entity->bone_count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bi = entity->body_indices[i];
        if (bi == UINT32_MAX) continue;

        phys_body_t *body = phys_world_get_body(world, bi);
        if (!body) continue;

        /* Only update kinematic bodies — physics owns dynamic ones. */
        if (!phys_body_is_kinematic(body)) continue;

        body->position = (phys_vec3_t){
            world_pose[i].m[12],
            world_pose[i].m[13],
            world_pose[i].m[14]
        };
        body->orientation = quat_from_mat4(&world_pose[i]);
    }
}
