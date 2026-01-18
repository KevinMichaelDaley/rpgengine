#include <stdlib.h>

#include "ferrum/ecs/world.h"

ecs_status_t ecs_world_register_set(ecs_world_t *world, ecs_sparse_set_base_t *set) {
    if (world == NULL || set == NULL) {
        return ECS_ERR_INVALID;
    }
    if (world->set_count == world->set_capacity) {
        uint32_t new_capacity = world->set_capacity == 0u ? 4u : world->set_capacity * 2u;
        ecs_sparse_set_base_t **next = realloc(world->sets, new_capacity * sizeof(*next));
        if (next == NULL) {
            return ECS_ERR_OOM;
        }
        world->sets = next;
        world->set_capacity = new_capacity;
    }
    world->sets[world->set_count++] = set;
    return ECS_OK;
}
