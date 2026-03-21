/**
 * @file prefab_collect.h
 * @brief Collect prefab data from the entity store.
 *
 * Scans entities parented to the prefab root and builds a
 * prefab_def_t from their collider shapes and hull markers.
 *
 * Ownership: output def is value-typed.
 * Nullability: all pointer params must be non-NULL.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_COLLECT_H
#define FERRUM_EDITOR_SCENE_PREFAB_COLLECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct prefab_def;
struct edit_entity_store;

/**
 * @brief Collect prefab definition from the entity store.
 *
 * Scans the entity store for colliders and markers parented to root_id
 * and builds a prefab_def_t with per-bone collider shapes and hull vertices.
 *
 * @param def        Output definition (non-NULL).
 * @param entities   Entity store (non-NULL).
 * @param root_id    Prefab root entity ID.
 * @param bone_count Number of bones in the skeleton.
 * @return true on success, false on invalid args.
 */
bool prefab_collect_from_entities(struct prefab_def *def,
                                 const struct edit_entity_store *entities,
                                 uint32_t root_id,
                                 uint32_t bone_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_COLLECT_H */
