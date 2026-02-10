#include "ferrum/physics/world.h"

#include <stddef.h>

void phys_world_set_sphere_collider(phys_world_t *world, uint32_t body_index,
                                    float radius, phys_vec3_t offset) {
    if (!world) {
        return;
    }
    if (body_index >= world->body_pool.capacity) {
        return;
    }

    /* Store shape data. */
    uint32_t si = world->sphere_count++;
    world->spheres[si].radius = radius;

    /* Set collider reference. */
    phys_collider_init_sphere(&world->colliders[body_index], si, offset);

    phys_body_t *b = phys_body_pool_get_curr(&world->body_pool, body_index);
    if (b && world->static_bvh_valid && phys_body_is_static(b)) {
        phys_world_static_bvh_invalidate(world);
    }
}

void phys_world_set_box_collider(phys_world_t *world, uint32_t body_index,
                                 phys_vec3_t half_extents, phys_vec3_t offset,
                                 phys_quat_t rotation) {
    if (!world) {
        return;
    }
    if (body_index >= world->body_pool.capacity) {
        return;
    }

    /* Store shape data. */
    uint32_t si = world->box_count++;
    world->boxes[si].half_extents = half_extents;

    /* Set collider reference. */
    phys_collider_init_box(&world->colliders[body_index], si, offset, rotation);

    phys_body_t *b = phys_body_pool_get_curr(&world->body_pool, body_index);
    if (b && world->static_bvh_valid && phys_body_is_static(b)) {
        phys_world_static_bvh_invalidate(world);
    }
}

void phys_world_set_capsule_collider(phys_world_t *world, uint32_t body_index,
                                     float radius, float half_height,
                                     phys_vec3_t offset, phys_quat_t rotation) {
    if (!world) {
        return;
    }
    if (body_index >= world->body_pool.capacity) {
        return;
    }

    /* Store shape data. */
    uint32_t si = world->capsule_count++;
    world->capsules[si].radius      = radius;
    world->capsules[si].half_height  = half_height;

    /* Set collider reference. */
    phys_collider_init_capsule(&world->colliders[body_index], si, offset, rotation);

    phys_body_t *b = phys_body_pool_get_curr(&world->body_pool, body_index);
    if (b && world->static_bvh_valid && phys_body_is_static(b)) {
        phys_world_static_bvh_invalidate(world);
    }
}

const phys_collider_t *phys_world_get_collider(const phys_world_t *world,
                                               uint32_t body_index) {
    if (!world) {
        return NULL;
    }
    if (body_index >= world->body_pool.capacity) {
        return NULL;
    }
    if (!phys_body_pool_is_active(&world->body_pool, body_index)) {
        return NULL;
    }
    return &world->colliders[body_index];
}
