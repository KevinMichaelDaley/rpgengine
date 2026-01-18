#ifndef FERRUM_ECS_ENTITY_H
#define FERRUM_ECS_ENTITY_H

#include <stdint.h>

#include "ferrum/ecs/common.h"

/** @file
 * @brief Entity allocator with generation counters.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Entity handle. */
typedef struct entity {
    uint32_t index;
    uint32_t generation;
} entity_t;

/** Entity pool allocator. */
typedef struct ecs_entity_pool {
    uint32_t capacity;
    uint32_t live_count;
    uint32_t *free_list;
    uint32_t free_head;
    uint32_t *generations;
} ecs_entity_pool_t;

/**
 * @brief Initialize entity pool.
 * @param pool Pool pointer.
 * @param capacity Maximum entities.
 * @return ECS_OK on success.
 */
ecs_status_t ecs_entity_pool_init(ecs_entity_pool_t *pool, uint32_t capacity);

/**
 * @brief Destroy entity pool.
 * @param pool Pool pointer.
 */
void ecs_entity_pool_destroy(ecs_entity_pool_t *pool);

/**
 * @brief Create a new entity.
 * @param pool Pool pointer.
 * @param out_entity Output entity handle.
 * @return ECS_OK on success, ECS_ERR_FULL if exhausted.
 */
ecs_status_t ecs_entity_create(ecs_entity_pool_t *pool, entity_t *out_entity);

/**
 * @brief Destroy an entity.
 * @param pool Pool pointer.
 * @param entity Entity handle.
 * @return ECS_OK on success, ECS_ERR_INVALID if not alive.
 */
ecs_status_t ecs_entity_destroy(ecs_entity_pool_t *pool, entity_t entity);

/**
 * @brief Check if entity is alive.
 * @param pool Pool pointer.
 * @param entity Entity handle.
 * @return Non-zero if alive.
 */
int ecs_entity_is_alive(const ecs_entity_pool_t *pool, entity_t entity);

/**
 * @brief Count live entities.
 * @param pool Pool pointer.
 * @return Live entity count.
 */
uint32_t ecs_entity_live_count(const ecs_entity_pool_t *pool);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_ECS_ENTITY_H */
