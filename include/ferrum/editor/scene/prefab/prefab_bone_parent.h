/**
 * @file prefab_bone_parent.h
 * @brief Parent/unparent entities to skeleton bones via entity attrs.
 *
 * Sets SCRIPT_KEY_PARENT_ID and SCRIPT_KEY_BONE_INDEX on an entity
 * to express "this collider belongs to bone N of entity M".
 *
 * Ownership: does not take ownership; modifies attrs on the entity.
 * Nullability: store must be non-NULL.
 * Error semantics: returns false on invalid entity or NULL store.
 * Side effects: modifies entity attrs in the store.
 *
 * Public types: none (0-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_BONE_PARENT_H
#define FERRUM_EDITOR_SCENE_PREFAB_BONE_PARENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration. */
struct edit_entity_store;

/**
 * @brief Parent an entity to a bone of the prefab root.
 *
 * Sets SCRIPT_KEY_PARENT_ID = root_entity_id and
 * SCRIPT_KEY_BONE_INDEX = bone_index on the entity.
 *
 * @param store          Entity store (non-NULL).
 * @param entity_id      Entity to parent.
 * @param root_entity_id Prefab root entity ID (stored as PARENT_ID).
 * @param bone_index     Bone index within the skeleton.
 * @return true on success, false if entity is invalid or store is NULL.
 */
bool prefab_parent_to_bone(struct edit_entity_store *store,
                           uint32_t entity_id,
                           uint32_t root_entity_id,
                           uint32_t bone_index);

/**
 * @brief Remove bone parenting from an entity.
 *
 * Removes SCRIPT_KEY_PARENT_ID and SCRIPT_KEY_BONE_INDEX attrs.
 *
 * @param store      Entity store (non-NULL).
 * @param entity_id  Entity to unparent.
 * @return true if attrs were removed, false if entity is invalid or
 *         had no parent attrs.
 */
bool prefab_unparent(struct edit_entity_store *store, uint32_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_BONE_PARENT_H */
