/**
 * @file skeleton_mode.h
 * @brief Skeleton editing mode state (K key).
 *
 * Skeleton mode edits the .fskel file directly without modifying
 * entities, prefabs, or meshes. A ghost preview of the bound mesh
 * renders for reference but is read-only.
 *
 * Thread safety: must be accessed from the main render thread only.
 */
#ifndef FERRUM_EDITOR_SCENE_SKELETON_MODE_H
#define FERRUM_EDITOR_SCENE_SKELETON_MODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/vec3.h"

/** @brief Maximum entities hidden when entering skeleton mode. */
#define SKELETON_MODE_MAX_HIDDEN 4096

/**
 * @brief In-progress bone creation drag state.
 */
typedef struct bone_create_drag {
    bool     active;        /**< Currently dragging to set tail. */
    vec3_t   head;          /**< World-space head position (set on click). */
    vec3_t   tail;          /**< World-space tail position (follows mouse). */
    uint32_t parent_bone;   /**< Parent bone index (UINT32_MAX = root). */
} bone_create_drag_t;

/**
 * @brief Skeleton mode state.
 *
 * Tracks the active skeleton being edited, hidden entities,
 * bone creation drag, and head/tail fine-tuning mode.
 */
typedef struct skeleton_mode_state {
    bool     active;                    /**< True when skeleton mode is on. */
    uint32_t entity_id;                 /**< Entity whose skeleton we're editing. */
    char     skel_path[256];            /**< Skeleton filename (registry key). */
    char     skel_full_path[512];       /**< Full path for saving. */
    bone_create_drag_t create_drag;     /**< In-progress bone placement. */

    /** Hidden entity IDs (restored on exit). */
    uint32_t hidden_ids[SKELETON_MODE_MAX_HIDDEN];
    uint32_t hidden_count;
} skeleton_mode_state_t;

/* Forward declarations. */
struct scene_editor;

/**
 * @brief Enter skeleton mode for the currently selected entity.
 *
 * Requires exactly one entity selected with a SKEL_PATH attribute
 * (or creates an empty skeleton if none exists).
 *
 * @param ed  Scene editor context.
 * @return true on success, false if requirements not met.
 */
bool skeleton_mode_enter(struct scene_editor *ed);

/**
 * @brief Exit skeleton mode without saving.
 *
 * Restores hidden entities and clears skeleton mode state.
 * Does NOT auto-save — user must :w explicitly.
 *
 * @param ed  Scene editor context.
 */
void skeleton_mode_exit(struct scene_editor *ed);

/**
 * @brief Reset skeleton mode state to inactive defaults.
 * @param state  State to reset.
 */
void skeleton_mode_state_reset(skeleton_mode_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_SKELETON_MODE_H */
