#include "ferrum/ecs/world.h"

ecs_status_t ecs_world_create_entity(ecs_world_t *world, entity_t *out_entity) {
    if (world == NULL) {
        return ECS_ERR_INVALID;
    }
    return ecs_entity_create(&world->entity_pool, out_entity);
}

ecs_status_t ecs_world_destroy_entity(ecs_world_t *world, entity_t entity) {
    if (world == NULL) {
        return ECS_ERR_INVALID;
    }
    ecs_status_t status = ecs_entity_destroy(&world->entity_pool, entity);
    if (status != ECS_OK) {
        return status;
    }
    for (uint32_t i = 0; i < world->set_count; ++i) {
        ecs_sparse_set_base_t *set = world->sets[i];
        if (set != NULL) {
            ecs_sparse_set_base_remove(set, entity);
        }
    }
    return ECS_OK;
}
