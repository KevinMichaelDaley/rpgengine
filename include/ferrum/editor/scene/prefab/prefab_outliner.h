/**
 * @file prefab_outliner.h
 * @brief Prefab outliner tree: bone hierarchy with nested colliders.
 *
 * Builds a flat list of entries representing the skeleton bone
 * hierarchy with collider entities nested under their parent bones.
 * Used by the outliner UI in prefab mode to display the hierarchy.
 *
 * Ownership: prefab_outliner_t is value-typed, no heap allocations.
 * Nullability: all pointer params must be non-NULL unless documented.
 *
 * Public types: prefab_outliner_entry_t, prefab_outliner_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_OUTLINER_H
#define FERRUM_EDITOR_SCENE_PREFAB_OUTLINER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** Maximum entries in the prefab outliner tree. */
#define PREFAB_OUTLINER_MAX_ENTRIES 2048

/** Maximum name length for an outliner entry. */
#define PREFAB_OUTLINER_NAME_MAX 64

/**
 * @brief A single entry in the prefab outliner tree.
 *
 * Can represent either a skeleton bone or a collider entity
 * parented to a bone.
 */
typedef struct prefab_outliner_entry {
    uint32_t bone_index;                       /**< Bone index (for both bones and colliders). */
    char     name[PREFAB_OUTLINER_NAME_MAX];   /**< Display name. */
    uint8_t  indent;                           /**< Tree indent level (0 = root). */
    bool     is_bone;                          /**< True for bone, false for entity. */
    uint32_t entity_id;                        /**< Entity ID (UINT32_MAX for bones). */
} prefab_outliner_entry_t;

/**
 * @brief Prefab outliner tree — flat list of hierarchical entries.
 */
typedef struct prefab_outliner {
    prefab_outliner_entry_t entries[PREFAB_OUTLINER_MAX_ENTRIES];
    uint32_t count;     /**< Number of active entries. */
} prefab_outliner_t;

/* ---- Lifecycle (prefab_outliner_build.c) ---- */

/* Forward declarations. */
struct skeleton_def;
struct edit_entity_store;

/**
 * @brief Initialize the outliner tree to empty.
 * @param tree  Tree to initialize (non-NULL).
 */
void prefab_outliner_init(prefab_outliner_t *tree);

/**
 * @brief Build the outliner tree from a skeleton and entity store.
 *
 * Performs a DFS of the bone hierarchy. For each bone, appends a bone
 * entry, then appends any entities in the store that have
 * PARENT_ID == root_id and BONE_INDEX == bone_index.
 *
 * @param tree     Outliner tree to populate (non-NULL, reset on call).
 * @param skel     Skeleton definition (non-NULL).
 * @param entities Entity store to scan for parented colliders (non-NULL).
 * @param root_id  Prefab root entity ID (to match PARENT_ID attrs).
 */
void prefab_outliner_build(prefab_outliner_t *tree,
                           const struct skeleton_def *skel,
                           const struct edit_entity_store *entities,
                           uint32_t root_id);

/* ---- Query (prefab_outliner_query.c) ---- */

/**
 * @brief Return the number of entries in the tree.
 * @param tree  Outliner tree (non-NULL).
 * @return Entry count.
 */
uint32_t prefab_outliner_count(const prefab_outliner_t *tree);

/**
 * @brief Get an entry by index.
 * @param tree   Outliner tree (non-NULL).
 * @param index  Entry index.
 * @return Pointer to entry, or NULL if out of range.
 */
const prefab_outliner_entry_t *prefab_outliner_get(
    const prefab_outliner_t *tree, uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_OUTLINER_H */
