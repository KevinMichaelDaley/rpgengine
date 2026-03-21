/**
 * @file bone_pose_store.c
 * @brief Per-entity bone pose store: init, destroy, ensure, remove.
 *
 * Uses vm_reserve for the entity→block index mapping (demand-paged,
 * zero physical cost for unused entity IDs) and a heap-allocated
 * pool of bone_pose_block_t blocks with a freelist for recycling.
 *
 * Non-static functions (4 / 4 limit):
 *   1. bone_pose_store_init
 *   2. bone_pose_store_destroy
 *   3. bone_pose_store_ensure
 *   4. bone_pose_store_remove
 */

#include "ferrum/editor/scene/bone_pose/bone_pose_store.h"
#include "ferrum/memory/vm_alloc.h"

#include <stdlib.h>
#include <string.h>

bool bone_pose_store_init(bone_pose_store_t *store, uint32_t entity_capacity) {
    if (!store || entity_capacity == 0) return false;

    memset(store, 0, sizeof(*store));

    /* vm_reserve the entity→block slot array. Pages are demand-paged,
     * so this is cheap even for large entity_capacity values. */
    size_t slot_bytes = (size_t)entity_capacity * sizeof(uint32_t);
    store->entity_slot = (uint32_t *)vm_reserve(slot_bytes);
    if (!store->entity_slot) return false;

    /* Initialize all slots to BONE_POSE_SLOT_NONE.
     * vm_reserve gives us zero-initialized memory, but SLOT_NONE = UINT32_MAX,
     * so we need to explicitly set them. However, for demand-paged memory
     * we only want to touch pages that will be used. Use memset for now —
     * the OS will page in only what we touch. */
    memset(store->entity_slot, 0xFF, slot_bytes);
    store->entity_cap = entity_capacity;

    /* Allocate block pool. */
    store->block_cap = BONE_POSE_MAX_BLOCKS;
    store->blocks = (bone_pose_block_t *)calloc(store->block_cap,
                                                 sizeof(bone_pose_block_t));
    if (!store->blocks) {
        vm_release(store->entity_slot, slot_bytes);
        store->entity_slot = NULL;
        return false;
    }

    /* Allocate freelist (max = block_cap entries). */
    store->freelist = (uint32_t *)calloc(store->block_cap, sizeof(uint32_t));
    if (!store->freelist) {
        free(store->blocks);
        vm_release(store->entity_slot, slot_bytes);
        memset(store, 0, sizeof(*store));
        return false;
    }
    store->freelist_count = 0;
    store->block_count = 0;

    return true;
}

void bone_pose_store_destroy(bone_pose_store_t *store) {
    if (!store) return;

    if (store->entity_slot) {
        vm_release(store->entity_slot,
                   (size_t)store->entity_cap * sizeof(uint32_t));
    }
    free(store->blocks);
    free(store->freelist);
    memset(store, 0, sizeof(*store));
}

bone_pose_block_t *bone_pose_store_ensure(bone_pose_store_t *store,
                                           uint32_t entity_id,
                                           const skeleton_def_t *skel) {
    if (!store || !skel) return NULL;
    if (skel->joint_count == 0) return NULL;
    if (entity_id >= store->entity_cap) return NULL;

    /* If already has an override, return it. */
    uint32_t slot = store->entity_slot[entity_id];
    if (slot != BONE_POSE_SLOT_NONE && slot < store->block_cap) {
        bone_pose_block_t *existing = &store->blocks[slot];
        if (existing->active && existing->entity_id == entity_id) {
            return existing;
        }
    }

    /* Allocate a new block: prefer freelist, then append. */
    uint32_t new_slot;
    if (store->freelist_count > 0) {
        new_slot = store->freelist[--store->freelist_count];
    } else if (store->block_count < store->block_cap) {
        new_slot = store->block_count;
    } else {
        return NULL;  /* Pool full. */
    }

    bone_pose_block_t *block = &store->blocks[new_slot];
    memset(block, 0, sizeof(*block));

    block->entity_id = entity_id;
    uint32_t n = skel->joint_count < BONE_POSE_MAX_BONES
                     ? skel->joint_count : BONE_POSE_MAX_BONES;
    block->bone_count = n;
    block->active = true;

    /* Clone rest_local. */
    if (skel->rest_local) {
        memcpy(block->rest_local, skel->rest_local, n * sizeof(mat4_t));
    }

    /* Clone rest_world. */
    if (skel->rest_world) {
        memcpy(block->rest_world, skel->rest_world, n * sizeof(mat4_t));
    }

    /* Clone tail_positions. */
    if (skel->tail_positions) {
        memcpy(block->tail_positions, skel->tail_positions,
               n * 3 * sizeof(float));
    }

    /* Update index. */
    store->entity_slot[entity_id] = new_slot;

    /* Only increment block_count if we didn't use freelist
     * (freelist blocks are already counted in the pool). */
    if (new_slot == store->block_count) {
        store->block_count++;
    }

    return block;
}

void bone_pose_store_remove(bone_pose_store_t *store, uint32_t entity_id) {
    if (!store) return;
    if (entity_id >= store->entity_cap) return;

    uint32_t slot = store->entity_slot[entity_id];
    if (slot == BONE_POSE_SLOT_NONE || slot >= store->block_cap) return;

    bone_pose_block_t *block = &store->blocks[slot];
    if (!block->active || block->entity_id != entity_id) return;

    block->active = false;
    store->entity_slot[entity_id] = BONE_POSE_SLOT_NONE;

    /* Add to freelist for recycling. */
    if (store->freelist_count < store->block_cap) {
        store->freelist[store->freelist_count++] = slot;
    }
}
