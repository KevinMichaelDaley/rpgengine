/**
 * @file prefab_mode_state.h
 * @brief Prefab editor mode state types and lifecycle.
 *
 * Tracks whether prefab mode is active, which entity is the prefab
 * root, which entities were hidden on entry (to restore on exit),
 * and whether unsaved changes exist.
 *
 * Public types: prefab_mode_status_t, prefab_mode_state_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_MODE_STATE_H
#define FERRUM_EDITOR_SCENE_PREFAB_MODE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/editor/scene/prefab/prefab_child_spawn.h"

/** @brief Maximum entities that can be hidden when entering prefab mode. */
#define PREFAB_MODE_MAX_HIDDEN 4096

/**
 * @brief Prefab mode activation status.
 */
typedef enum prefab_mode_status {
    PREFAB_MODE_INACTIVE = 0,
    PREFAB_MODE_ACTIVE   = 1,
} prefab_mode_status_t;

/**
 * @brief Prefab editor mode state.
 *
 * Stored on the scene_editor_t. Tracks the current prefab editing
 * session including which entities were hidden so they can be restored
 * on exit.
 */
typedef struct prefab_mode_state {
    bool     active;                          /**< True when prefab mode is active. */
    uint32_t root_entity_id;                  /**< Entity being edited (UINT32_MAX = none). */
    char     name[256];                       /**< Display name for status bar. */
    char     fpfab_path[256];                 /**< Loaded/saved .fpfab path (empty = new). */
    uint32_t hidden_ids[PREFAB_MODE_MAX_HIDDEN]; /**< Entity IDs hidden on enter. */
    uint32_t hidden_count;                    /**< Number of hidden entity IDs. */
    bool     dirty;                           /**< True if unsaved changes exist. */
    uint32_t dirty_gen;                       /**< Incremented on each change; consumers track their own gen. */
    prefab_pending_spawn_t pending_spawns;    /**< Pending child spawn tracking. */
} prefab_mode_state_t;

/**
 * @brief Initialize prefab mode state to inactive defaults.
 * @param state  State to initialize (may be NULL, no-op).
 */
void prefab_mode_state_init(prefab_mode_state_t *state);

/**
 * @brief Reset prefab mode state to inactive (clear all fields).
 * @param state  State to reset (may be NULL, no-op).
 */
void prefab_mode_state_reset(prefab_mode_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_MODE_STATE_H */
