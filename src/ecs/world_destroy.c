#include <stdlib.h>

#include "ferrum/ecs/world.h"

void ecs_world_destroy(ecs_world_t *world) {
    if (world == NULL) {
        return;
    }
    free(world->sets);
    world->sets = NULL;
    world->set_count = 0u;
    world->set_capacity = 0u;
    ecs_entity_pool_destroy(&world->entity_pool);
}
