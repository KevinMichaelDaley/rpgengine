/**
 * @file bone_pose_store_query.c
 * @brief Bone pose store query functions: get, get_mut, has.
 *
 * Non-static functions (3 / 4 limit):
 *   1. bone_pose_store_get
 *   2. bone_pose_store_get_mut
 *   3. bone_pose_store_has
 */

#include "ferrum/editor/scene/bone_pose/bone_pose_store.h"

#include <stddef.h>

const bone_pose_block_t *bone_pose_store_get(const bone_pose_store_t *store,
                                              uint32_t entity_id) {
    if (!store) return NULL;
    if (entity_id >= store->entity_cap) return NULL;

    uint32_t slot = store->entity_slot[entity_id];
    if (slot == BONE_POSE_SLOT_NONE || slot >= store->block_cap) return NULL;

    const bone_pose_block_t *block = &store->blocks[slot];
    if (!block->active || block->entity_id != entity_id) return NULL;
    return block;
}

bone_pose_block_t *bone_pose_store_get_mut(bone_pose_store_t *store,
                                            uint32_t entity_id) {
    if (!store) return NULL;
    if (entity_id >= store->entity_cap) return NULL;

    uint32_t slot = store->entity_slot[entity_id];
    if (slot == BONE_POSE_SLOT_NONE || slot >= store->block_cap) return NULL;

    bone_pose_block_t *block = &store->blocks[slot];
    if (!block->active || block->entity_id != entity_id) return NULL;
    return block;
}

bool bone_pose_store_has(const bone_pose_store_t *store, uint32_t entity_id) {
    return bone_pose_store_get(store, entity_id) != NULL;
}
