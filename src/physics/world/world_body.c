#include "ferrum/physics/world.h"

#include <stddef.h>

uint32_t phys_world_create_body(phys_world_t *world) {
    if (!world) {
        return UINT32_MAX;
    }

    phys_body_t body;
    phys_body_init(&body);

    uint32_t index = 0;
    if (phys_body_pool_add(&world->body_pool, &body, &index) != 0) {
        return UINT32_MAX;
    }
    return index;
}

void phys_world_destroy_body(phys_world_t *world, uint32_t index) {
    if (!world) {
        return;
    }
    phys_body_pool_remove(&world->body_pool, index);
}

phys_body_t *phys_world_get_body(phys_world_t *world, uint32_t index) {
    if (!world) {
        return NULL;
    }
    return phys_body_pool_get_curr(&world->body_pool, index);
}

uint32_t phys_world_body_count(const phys_world_t *world) {
    if (!world) {
        return 0;
    }
    return phys_body_pool_active_count(&world->body_pool);
}
