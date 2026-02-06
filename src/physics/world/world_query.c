#include "ferrum/physics/world.h"

#include <stddef.h>

const phys_aabb_t *phys_world_get_aabb(const phys_world_t *world,
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
    return &world->aabbs[body_index];
}

uint64_t phys_world_tick_count(const phys_world_t *world) {
    if (!world) {
        return 0;
    }
    return world->tick_count;
}
