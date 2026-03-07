/**
 * @file phys_anim_entity_drive.c
 * @brief Apply animation-driven position/orientation deltas to bodies.
 *
 * For kinematic bodies, sets absolute position+orientation.
 * For dynamic bodies, blends current pose toward the animation
 * target by applying interpolated position and orientation deltas.
 *
 * Non-static functions: 1 (phys_anim_entity_drive_toward)
 */

#include "ferrum/physics/phys_anim_entity.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

#include <stddef.h>

void phys_anim_entity_drive_toward(phys_anim_entity_t *entity,
                                   phys_world_t *world,
                                   const mat4_t *world_pose,
                                   uint32_t count,
                                   float blend) {
    if (!entity || !world || !world_pose) return;
    if (blend <= 0.0f) return;

    uint32_t n = count < entity->bone_count ? count : entity->bone_count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t bi = entity->body_indices[i];
        if (bi == UINT32_MAX) continue;

        phys_body_t *body = phys_world_get_body(world, bi);
        if (!body) continue;

        phys_vec3_t target_pos = {
            world_pose[i].m[12],
            world_pose[i].m[13],
            world_pose[i].m[14]
        };
        quat_t target_ori = quat_from_mat4(&world_pose[i]);

        if (phys_body_is_kinematic(body)) {
            /* Kinematic: set absolute pose. */
            body->position    = target_pos;
            body->orientation = target_ori;
        } else {
            /* Dynamic: interpolate toward target. */
            float t = (blend > 1.0f) ? 1.0f : blend;

            /* Position: current + t * (target - current). */
            body->position.x += t * (target_pos.x - body->position.x);
            body->position.y += t * (target_pos.y - body->position.y);
            body->position.z += t * (target_pos.z - body->position.z);

            /* Orientation: slerp from current toward target. */
            body->orientation = quat_slerp(body->orientation,
                                           target_ori, t, 1e-6f);
        }
    }
}
