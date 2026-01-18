#ifndef FERRUM_ECS_SPARSE_SET_H
#define FERRUM_ECS_SPARSE_SET_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/ecs/common.h"
#include "ferrum/ecs/entity.h"

/** @file
 * @brief Macro-generated sparse set storage.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Base sparse set metadata for world registration. */
typedef struct ecs_sparse_set_base {
    void *dense;
    entity_t *dense_entities;
    uint32_t *sparse;
    uint32_t capacity;
    uint32_t size;
    size_t stride;
} ecs_sparse_set_base_t;

#define ECS_DEFINE_SPARSE_SET(name, type)                                                              \
    typedef struct ecs_sparse_set_##name {                                                             \
        ecs_sparse_set_base_t base;                                                                    \
    } ecs_sparse_set_##name##_t;                                                                       \
                                                                                                       \
    static inline ecs_status_t ecs_sparse_set_##name##_init(ecs_sparse_set_##name##_t *set,            \
                                                            uint32_t capacity) {                       \
        return ecs_sparse_set_base_init(&set->base, capacity, sizeof(type));                            \
    }                                                                                                  \
    static inline void ecs_sparse_set_##name##_destroy(ecs_sparse_set_##name##_t *set) {               \
        ecs_sparse_set_base_destroy(&set->base);                                                       \
    }                                                                                                  \
    static inline ecs_status_t ecs_sparse_set_##name##_insert(ecs_sparse_set_##name##_t *set,          \
                                                              entity_t entity, const type *value) {    \
        return ecs_sparse_set_base_insert(&set->base, entity, value);                                   \
    }                                                                                                  \
    static inline ecs_status_t ecs_sparse_set_##name##_remove(ecs_sparse_set_##name##_t *set,          \
                                                              entity_t entity) {                       \
        return ecs_sparse_set_base_remove(&set->base, entity);                                          \
    }                                                                                                  \
    static inline type *ecs_sparse_set_##name##_get(ecs_sparse_set_##name##_t *set, entity_t entity) { \
        return (type *)ecs_sparse_set_base_get(&set->base, entity);                                     \
    }                                                                                                  \
    static inline uint32_t ecs_sparse_set_##name##_size(const ecs_sparse_set_##name##_t *set) {        \
        return set->base.size;                                                                         \
    }                                                                                                  \
    static inline type *ecs_sparse_set_##name##_dense(ecs_sparse_set_##name##_t *set) {                \
        return (type *)set->base.dense;                                                                 \
    }                                                                                                  \
    static inline entity_t *ecs_sparse_set_##name##_entities(ecs_sparse_set_##name##_t *set) {         \
        return set->base.dense_entities;                                                                \
    }                                                                                                  \
    static inline const uint32_t *ecs_sparse_set_##name##_sparse(const ecs_sparse_set_##name##_t *set) { \
        return set->base.sparse;                                                                         \
    }

ecs_status_t ecs_sparse_set_base_init(ecs_sparse_set_base_t *base, uint32_t capacity, size_t stride);
void ecs_sparse_set_base_destroy(ecs_sparse_set_base_t *base);
ecs_status_t ecs_sparse_set_base_insert(ecs_sparse_set_base_t *base, entity_t entity, const void *value);
ecs_status_t ecs_sparse_set_base_remove(ecs_sparse_set_base_t *base, entity_t entity);
void *ecs_sparse_set_base_get(ecs_sparse_set_base_t *base, entity_t entity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_ECS_SPARSE_SET_H */
