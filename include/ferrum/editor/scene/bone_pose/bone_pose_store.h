/**
 * @file bone_pose_store.h
 * @brief Per-entity bone pose override storage.
 *
 * Stores independent bone pose overrides so that editing bones on an entity
 * in regular edit mode does not mutate the shared skeleton in the registry.
 * Uses a vm_reserve'd entity→block index mapping and a heap pool of blocks.
 *
 * Public types (2-type rule):
 *   1. bone_pose_block_t  — per-entity bone pose data
 *   2. bone_pose_store_t  — the store (index + pool)
 *
 * Ownership: the store owns all blocks. Destroy with bone_pose_store_destroy().
 * Nullability: all pointer params must be non-NULL unless documented.
 * Error semantics: ensure returns NULL on error (pool full, bad args).
 */
#ifndef FERRUM_EDITOR_SCENE_BONE_POSE_STORE_H
#define FERRUM_EDITOR_SCENE_BONE_POSE_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/mat4.h"
#include "ferrum/animation/constraint_params.h"

/** Maximum bones per pose block. Matches PREFAB_MAX_BONES. */
#define BONE_POSE_MAX_BONES 256u

/** Maximum blocks in the pool. */
#define BONE_POSE_MAX_BLOCKS 256u

/** Sentinel value for entity_slot (no override for this entity). */
#define BONE_POSE_SLOT_NONE UINT32_MAX

/**
 * @brief Per-entity bone pose override data.
 *
 * Contains cloned rest_local, rest_world, and tail_positions arrays.
 * Only entities with bone overrides consume a block.
 */
typedef struct bone_pose_block {
    uint32_t entity_id;                         /**< Entity this block belongs to. */
    uint32_t bone_count;                        /**< Number of active bones. */
    mat4_t   rest_local[BONE_POSE_MAX_BONES];   /**< Local rest transforms (editable). */
    mat4_t   rest_world[BONE_POSE_MAX_BONES];   /**< World rest transforms (recomputed). */
    float    tail_positions[BONE_POSE_MAX_BONES * 3]; /**< Tail positions (x,y,z per bone). */
    bool     active;                            /**< True if block is in use. */
} bone_pose_block_t;

/**
 * @brief Per-entity bone pose store with index + pool design.
 *
 * entity_slot is vm_reserve'd (indexed by entity ID, BONE_POSE_SLOT_NONE = no override).
 * blocks is a heap-allocated pool array.
 * freelist tracks recycled block indices.
 */
typedef struct bone_pose_store {
    uint32_t *entity_slot;      /**< vm_reserve'd, entity_cap entries. */
    uint32_t  entity_cap;       /**< Size of entity_slot array. */

    bone_pose_block_t *blocks;  /**< Heap array of blocks. */
    uint32_t  block_count;      /**< Number of active (non-recycled) blocks. */
    uint32_t  block_cap;        /**< Total capacity of blocks array. */

    uint32_t *freelist;         /**< Recycled block indices. */
    uint32_t  freelist_count;   /**< Number of entries in freelist. */
} bone_pose_store_t;

/**
 * @brief Initialize the bone pose store.
 *
 * @param store          Store to initialize (non-NULL).
 * @param entity_capacity  Maximum entity ID supported (determines slot array size).
 * @return true on success, false on invalid args or allocation failure.
 */
bool bone_pose_store_init(bone_pose_store_t *store, uint32_t entity_capacity);

/**
 * @brief Destroy the bone pose store and free all memory.
 * @param store  Store to destroy (NULL-safe).
 */
void bone_pose_store_destroy(bone_pose_store_t *store);

/**
 * @brief Ensure an override block exists for the given entity.
 *
 * If the entity already has an override, returns the existing block.
 * Otherwise allocates a new block and clones rest_local, rest_world,
 * and tail_positions from the given skeleton.
 *
 * @param store      Store (non-NULL).
 * @param entity_id  Entity ID (must be < entity_capacity).
 * @param skel       Skeleton to clone from (non-NULL, joint_count > 0).
 * @return Pointer to the block, or NULL on error (pool full, bad args).
 */
bone_pose_block_t *bone_pose_store_ensure(bone_pose_store_t *store,
                                           uint32_t entity_id,
                                           const skeleton_def_t *skel);

/**
 * @brief Remove the override for an entity, recycling the block.
 * @param store      Store (NULL-safe).
 * @param entity_id  Entity ID.
 */
void bone_pose_store_remove(bone_pose_store_t *store, uint32_t entity_id);

/**
 * @brief Get a read-only pointer to an entity's bone pose override.
 * @param store      Store (NULL-safe).
 * @param entity_id  Entity ID.
 * @return Pointer to the block, or NULL if no override exists.
 */
const bone_pose_block_t *bone_pose_store_get(const bone_pose_store_t *store,
                                              uint32_t entity_id);

/**
 * @brief Get a mutable pointer to an entity's bone pose override.
 * @param store      Store (NULL-safe).
 * @param entity_id  Entity ID.
 * @return Mutable pointer to the block, or NULL if no override exists.
 */
bone_pose_block_t *bone_pose_store_get_mut(bone_pose_store_t *store,
                                            uint32_t entity_id);

/**
 * @brief Check whether an entity has a bone pose override.
 * @param store      Store (NULL-safe).
 * @param entity_id  Entity ID.
 * @return true if an override exists.
 */
bool bone_pose_store_has(const bone_pose_store_t *store, uint32_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_BONE_POSE_STORE_H */
