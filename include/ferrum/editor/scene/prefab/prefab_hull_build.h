/**
 * @file prefab_hull_build.h
 * @brief Build convex hulls from marker entities parented to bones.
 *
 * Scans the entity store for MARKER entities that are parented to a
 * specific bone of the prefab root, collects their positions, and
 * calls phys_convex_hull_build() to produce a convex hull.
 *
 * Ownership: result hull is self-contained (value type).
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: returns false if fewer than 4 markers.
 *
 * Public types: prefab_hull_result_t (1-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_HULL_BUILD_H
#define FERRUM_EDITOR_SCENE_PREFAB_HULL_BUILD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/convex_hull.h"

/* Forward declaration. */
struct edit_entity_store;

/**
 * @brief Result of building a hull from markers.
 */
typedef struct prefab_hull_result {
    phys_convex_hull_t hull;   /**< Built hull (valid only if valid==true). */
    bool               valid;  /**< True if hull was successfully built. */
    uint32_t           marker_count; /**< Number of markers found for this bone. */
} prefab_hull_result_t;

/**
 * @brief Build a convex hull from marker positions for a bone.
 *
 * Scans entities for active MARKERs with PARENT_ID == root_id and
 * BONE_INDEX == bone_index. Needs >=4 markers for a valid hull.
 *
 * @param entities   Entity store (non-NULL).
 * @param root_id    Prefab root entity ID.
 * @param bone_index Target bone index.
 * @param out        Output result (non-NULL).
 * @return true if hull built successfully, false otherwise.
 */
bool prefab_hull_build_from_markers(const struct edit_entity_store *entities,
                                    uint32_t root_id,
                                    uint32_t bone_index,
                                    prefab_hull_result_t *out);

/**
 * @brief Count marker entities parented to a bone.
 *
 * @param entities   Entity store (non-NULL).
 * @param root_id    Prefab root entity ID.
 * @param bone_index Target bone index.
 * @return Number of active MARKER entities parented to this bone.
 */
uint32_t prefab_hull_count_markers(const struct edit_entity_store *entities,
                                   uint32_t root_id,
                                   uint32_t bone_index);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_HULL_BUILD_H */
