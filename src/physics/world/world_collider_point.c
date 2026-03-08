/**
 * @file world_collider_point.c
 * @brief Set point collider on a physics body.
 *
 * Point colliders have no shape data — they generate a single contact
 * at the body center when it penetrates another shape (typically
 * a halfspace/ground plane).  Used for skeleton bones that need
 * ground contact but have no authored collision geometry.
 *
 * Non-static functions: 1 (phys_world_set_point_collider)
 */

#include "ferrum/physics/world.h"

#include <stddef.h>

void phys_world_set_point_collider(phys_world_t *world, uint32_t body_index,
                                    phys_vec3_t offset) {
    if (!world) return;
    if (body_index >= world->body_pool.capacity) return;

    /* Point colliders need no shape pool — just set the collider type. */
    phys_collider_init_point(&world->colliders[body_index], offset);

    phys_body_t *b = phys_body_pool_get_curr(&world->body_pool, body_index);
    if (b && world->static_bvh_valid && phys_body_is_static(b)) {
        phys_world_static_bvh_invalidate(world);
    }
}
