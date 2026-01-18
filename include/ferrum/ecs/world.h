#ifndef FERRUM_ECS_WORLD_H
#define FERRUM_ECS_WORLD_H

#include <stdint.h>

#include "ferrum/ecs/common.h"
#include "ferrum/ecs/entity.h"
#include "ferrum/ecs/sparse_set.h"

/** @file
 * @brief ECS world registry.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** World container for entities and component stores. */
typedef struct ecs_world {
    ecs_entity_pool_t entity_pool;
    ecs_sparse_set_base_t **sets;
    uint32_t set_count;
    uint32_t set_capacity;
} ecs_world_t;

/**
 * @brief Initialize world with entity capacity.
 * @param world World pointer.
 * @param entity_capacity Maximum entities.
 * @return ECS_OK on success.
 */
ecs_status_t ecs_world_init(ecs_world_t *world, uint32_t entity_capacity);

/**
 * @brief Destroy world resources.
 * @param world World pointer.
 */
void ecs_world_destroy(ecs_world_t *world);

/**
 * @brief Create an entity in the world.
 * @param world World pointer.
 * @param out_entity Output entity.
 * @return ECS_OK or ECS_ERR_FULL.
 */
ecs_status_t ecs_world_create_entity(ecs_world_t *world, entity_t *out_entity);

/**
 * @brief Destroy an entity in the world.
 * @param world World pointer.
 * @param entity Entity handle.
 * @return ECS_OK or ECS_ERR_INVALID.
 */
ecs_status_t ecs_world_destroy_entity(ecs_world_t *world, entity_t entity);

/**
 * @brief Register a sparse set with the world.
 * @param world World pointer.
 * @param set Sparse set base pointer.
 * @return ECS_OK on success.
 */
ecs_status_t ecs_world_register_set(ecs_world_t *world, ecs_sparse_set_base_t *set);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_ECS_WORLD_H */
