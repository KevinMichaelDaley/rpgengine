/**
 * @file prefab_child_spawn.h
 * @brief Prefab child spawn tracking (pending spawn queue).
 *
 * When in prefab mode, asset browser clicks spawn entities on the server.
 * This module tracks pending spawn command IDs and, when the entity list
 * refreshes, sets PARENT_ID on the new entity to parent it under the
 * prefab root.
 *
 * Ownership: caller owns prefab_pending_spawn_t (stack or embedded).
 * Nullability: all functions are NULL-safe.
 * Error semantics: overflow silently drops; out-of-range is no-op.
 * Side effects: resolve sets an attr on the target entity.
 *
 * Public types: prefab_pending_spawn_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_CHILD_SPAWN_H
#define FERRUM_EDITOR_SCENE_PREFAB_CHILD_SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief Maximum number of simultaneously pending spawn commands. */
#define PREFAB_PENDING_SPAWN_MAX 32

/* Forward declarations. */
struct edit_entity_store;

/**
 * @brief Tracks pending prefab child spawn command IDs.
 *
 * When a spawn command is sent while in prefab mode, its command ID
 * is added here. When the corresponding entity appears in the entity
 * list, resolve() sets PARENT_ID on it and decrements the count.
 */
typedef struct prefab_pending_spawn {
    uint32_t cmd_ids[PREFAB_PENDING_SPAWN_MAX]; /**< Pending command IDs. */
    uint32_t count;                              /**< Number of pending spawns. */
} prefab_pending_spawn_t;

/* ---- Lifecycle (prefab_child_spawn.c) ---- */

/**
 * @brief Initialize the pending spawn queue to empty.
 * @param ps  Pending spawn state (NULL-safe: no-op if NULL).
 */
void prefab_pending_spawn_init(prefab_pending_spawn_t *ps);

/**
 * @brief Add a spawn command ID to the pending queue.
 *
 * If the queue is full (PREFAB_PENDING_SPAWN_MAX), the add is silently
 * dropped.
 *
 * @param ps      Pending spawn state (NULL-safe: no-op if NULL).
 * @param cmd_id  Command ID from the spawn command.
 */
void prefab_pending_spawn_add(prefab_pending_spawn_t *ps, uint32_t cmd_id);

/**
 * @brief Resolve a newly appeared entity as a prefab child.
 *
 * Sets SCRIPT_KEY_PARENT_ID on the entity and decrements the pending
 * count. If the queue is empty or args are NULL, this is a no-op.
 *
 * @param ps              Pending spawn state (NULL-safe: no-op if NULL).
 * @param store           Entity store (NULL-safe: no-op if NULL).
 * @param new_entity_id   The newly spawned entity ID.
 * @param root_entity_id  Prefab root entity ID to set as parent.
 */
void prefab_pending_spawn_resolve(prefab_pending_spawn_t *ps,
                                   struct edit_entity_store *store,
                                   uint32_t new_entity_id,
                                   uint32_t root_entity_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_CHILD_SPAWN_H */
