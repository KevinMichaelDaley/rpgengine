/**
 * @file scene_node.h
 * @brief Per-entity scene node for the LCRS scene graph.
 *
 * Each entity in the scene has a corresponding scene_node_t stored in a
 * flat array parallel to the entity pool. The tree uses left-child
 * right-sibling (LCRS) representation for cache-friendly traversal.
 *
 * @note Entities without scene presence have all link fields set to
 *       SCENE_NODE_NONE (UINT32_MAX).
 */
#ifndef FERRUM_RENDERER_SCENE_NODE_H
#define FERRUM_RENDERER_SCENE_NODE_H

#include <stdint.h>
#include "ferrum/math/mat4.h"

/** Sentinel value indicating "no node" (unattached / end of list). */
#define SCENE_NODE_NONE UINT32_MAX

/** Node has a modified local transform that needs world recomputation. */
#define SCENE_NODE_DIRTY_LOCAL  (1u << 0)

/** Node's world transform is stale (parent moved). */
#define SCENE_NODE_DIRTY_WORLD  (1u << 1)

/** World transform is baked — skip during BFS update unless explicitly
 *  invalidated. */
#define SCENE_NODE_STATIC       (1u << 2)

/**
 * @brief Per-entity scene node in the LCRS tree.
 *
 * Stored in a flat array indexed by entity index. LCRS links use entity
 * indices (not pointers) for cache locality and serialization safety.
 */
typedef struct scene_node {
    uint32_t parent;          /**< Entity index of parent (SCENE_NODE_NONE = root). */
    uint32_t first_child;     /**< Entity index of first child (SCENE_NODE_NONE = leaf). */
    uint32_t next_sibling;    /**< Entity index of next sibling (SCENE_NODE_NONE = last). */
    uint32_t flags;           /**< Combination of SCENE_NODE_* flags. */
    mat4_t   local_transform; /**< Transform relative to parent. */
    mat4_t   world_transform; /**< Computed world-space transform. */
} scene_node_t;

#endif /* FERRUM_RENDERER_SCENE_NODE_H */
