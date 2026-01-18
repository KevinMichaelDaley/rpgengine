#include "ferrum/ecs/world.h"

ecs_status_t ecs_world_init(ecs_world_t *world, uint32_t entity_capacity) {
    if (world == NULL) {
        return ECS_ERR_INVALID;
    }
    world->sets = NULL;
    world->set_count = 0u;
    world->set_capacity = 0u;
    return ecs_entity_pool_init(&world->entity_pool, entity_capacity);
}
