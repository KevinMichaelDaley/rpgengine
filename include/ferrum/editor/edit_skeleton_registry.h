/**
 * @file edit_skeleton_registry.h
 * @brief Editor skeleton registry — stores loaded skeleton definitions.
 *
 * Manages a fixed-capacity array of loaded skeleton_def_t objects,
 * indexed by asset path. Used by the editor to store skeletons loaded
 * from .fskel files for binding to mesh entities.
 *
 * Ownership: the registry owns all stored skeleton_def_t and IBM arrays.
 *            Destroy with edit_skeleton_registry_destroy().
 * Nullability: all pointer params must be non-NULL unless noted.
 * Error semantics: add returns UINT32_MAX on full/error; load returns false.
 * Side effects: load performs file I/O.
 *
 * Public types: edit_skeleton_entry_t, edit_skeleton_registry_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_EDIT_SKELETON_REGISTRY_H
#define FERRUM_EDITOR_EDIT_SKELETON_REGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/** Maximum path length for a skeleton asset. */
#define EDIT_SKELETON_PATH_MAX 256

/**
 * @brief A single loaded skeleton entry in the registry.
 */
typedef struct edit_skeleton_entry {
    char            path[EDIT_SKELETON_PATH_MAX]; /**< Asset path (e.g. "humanoid.fskel"). */
    skeleton_def_t  skel;       /**< Loaded skeleton definition (owned). */
    mat4_t         *ibms;       /**< Inverse bind matrices (heap, owned, may be NULL). */
    uint32_t        ibm_count;  /**< Number of IBMs. */
    bool            active;     /**< True if this slot is in use. */
} edit_skeleton_entry_t;

/**
 * @brief Registry of loaded skeletons.
 */
typedef struct edit_skeleton_registry {
    edit_skeleton_entry_t *entries; /**< Heap-allocated entry array (owned). */
    uint32_t capacity;             /**< Total slots. */
    uint32_t count;                /**< Active entries. */
} edit_skeleton_registry_t;

/**
 * @brief Initialize the skeleton registry.
 *
 * @param reg       Registry to initialize (non-NULL).
 * @param capacity  Maximum number of skeletons (must be >= 1).
 * @return true on success, false on invalid args or allocation failure.
 */
bool edit_skeleton_registry_init(edit_skeleton_registry_t *reg,
                                  uint32_t capacity);

/**
 * @brief Destroy the registry and free all stored skeletons.
 * @param reg  Registry to destroy (non-NULL, safe if already destroyed).
 */
void edit_skeleton_registry_destroy(edit_skeleton_registry_t *reg);

/**
 * @brief Add a skeleton to the registry (takes ownership).
 *
 * The registry takes ownership of skel's internal allocations and the
 * ibms array. The caller's skeleton_def_t is zeroed on success to
 * prevent double-free. Do NOT call skeleton_def_destroy() after add.
 *
 * If a skeleton with the same path already exists, replaces it.
 *
 * @param reg        Registry (non-NULL).
 * @param path       Asset path key (non-NULL, non-empty).
 * @param skel       Skeleton to transfer into registry (non-NULL, zeroed on success).
 * @param ibms       Inverse bind matrices (ownership transferred, may be NULL).
 * @param ibm_count  Number of IBMs.
 * @return Slot index, or UINT32_MAX if registry is full.
 */
uint32_t edit_skeleton_registry_add(edit_skeleton_registry_t *reg,
                                     const char *path,
                                     skeleton_def_t *skel,
                                     mat4_t *ibms,
                                     uint32_t ibm_count);

/**
 * @brief Look up a skeleton by asset path.
 *
 * @param reg   Registry (non-NULL).
 * @param path  Asset path to search for (non-NULL).
 * @return Pointer to the entry, or NULL if not found.
 */
const edit_skeleton_entry_t *edit_skeleton_registry_get(
    const edit_skeleton_registry_t *reg, const char *path);

/**
 * @brief Look up a skeleton by asset path (mutable).
 *
 * Same as edit_skeleton_registry_get() but returns a mutable pointer.
 * Used by bone gizmo mode to modify rest pose transforms.
 *
 * @param reg   Registry (non-NULL).
 * @param path  Asset path to search for (non-NULL).
 * @return Mutable pointer to the entry, or NULL if not found.
 */
edit_skeleton_entry_t *edit_skeleton_registry_get_mut(
    edit_skeleton_registry_t *reg, const char *path);

/**
 * @brief Load a skeleton from an .fskel file and add to the registry.
 *
 * Extracts the filename from the full path for the registry key.
 *
 * @param reg        Registry (non-NULL).
 * @param full_path  Full path to .fskel file (non-NULL).
 * @return true on success, false on file/format error or registry full.
 */
bool edit_skeleton_registry_load(edit_skeleton_registry_t *reg,
                                  const char *full_path);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SKELETON_REGISTRY_H */
