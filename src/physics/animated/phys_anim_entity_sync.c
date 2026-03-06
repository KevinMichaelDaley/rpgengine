/**
 * @file phys_anim_entity_sync.c
 * @brief Sync animated entity bone transforms from/to physics world.
 *
 * Reads body positions + orientations from the world and writes them
 * as bone mat4_t transforms.  Also provides push_kinematic() to
 * write animation-driven bone poses into kinematic bodies.
 *
 * Non-static functions: 2 (phys_anim_entity_sync_from_world,
 *                          phys_anim_entity_push_kinematic)
 */

#include "ferrum/physics/phys_anim_entity.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

#include <stddef.h>

void phys_anim_entity_sync_from_world(phys_anim_entity_t *entity,
                                      const phys_world_t *world) {
    if (!entity || !world) return;

    for (uint32_t i = 0; i < entity->bone_count; i++) {
        uint32_t bi = entity->body_indices[i];
        if (bi == UINT32_MAX) continue;

        const phys_body_t *body = phys_world_get_body(
            (phys_world_t *)world, bi);
        if (!body) continue;

        /* Build rotation matrix from quaternion. */
        mat4_t rot;
        quat_to_mat4(body->orientation, &rot);

        /* Set translation from position. */
        rot.m[12] = body->position.x;
        rot.m[13] = body->position.y;
        rot.m[14] = body->position.z;

        entity->bone_world[i] = rot;
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
