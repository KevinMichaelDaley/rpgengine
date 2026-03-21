/**
 * @file prefab_def.h
 * @brief Prefab definition: entity tree snapshot for .fpfab files.
 *
 * A prefab captures an entity and its children (via PARENT_ID hierarchy)
 * along with all their inspector properties. Optionally includes per-bone
 * collider data when the root entity has a skeleton.
 *
 * Ownership: value type (inline arrays, no heap). Hull vertices inline.
 * Nullability: all pointer params must be non-NULL unless documented.
 *
 * Public types: prefab_entity_snapshot_t, prefab_def_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_DEF_H
#define FERRUM_EDITOR_SCENE_PREFAB_DEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/entity/entity_attrs.h"

/** Maximum entities in a prefab (root + children). */
#define PREFAB_MAX_ENTITIES 64

/** Maximum bones with collider overrides. */
#define PREFAB_MAX_BONES 256

/** Maximum total hull vertices across all bones. */
#define PREFAB_MAX_HULL_VERTS 4096

/** Current .fpfab format version. */
#define PREFAB_VERSION 1

/**
 * @brief Snapshot of a single entity within a prefab.
 *
 * Stores all serializable properties. Positions are relative to
 * the prefab root (world-space offsets from root position).
 */
typedef struct prefab_entity_snapshot {
    uint32_t type;                    /**< EDIT_ENTITY_TYPE_* */
    float    pos[3];                  /**< Position (relative to root). */
    float    rot[3];                  /**< Euler rotation (degrees). */
    float    scale[3];                /**< Scale factors. */
    char     name[256];               /**< Display name. */
    int32_t  local_parent;            /**< Index in entities[] of parent (-1 = root). */
    entity_attrs_t attrs;             /**< All dynamic key-value attributes. */
} prefab_entity_snapshot_t;

/**
 * @brief Complete prefab definition.
 *
 * Entity 0 is always the root. Entities 1..entity_count-1 are children.
 * Bone collider data is optional (bone_count == 0 if no skeleton).
 */
typedef struct prefab_def {
    uint32_t version;                /**< Format version (PREFAB_VERSION). */

    /* Entity hierarchy. */
    uint32_t entity_count;           /**< Total entities (root + children). */
    prefab_entity_snapshot_t entities[PREFAB_MAX_ENTITIES];

    /* Optional per-bone collider overrides (only when skeleton present). */
    uint32_t bone_count;             /**< Bones with collider data (0 = none). */
    struct {
        uint32_t shape_type;         /**< bone_collider_shape_t. */
        float    params[6];          /**< Shape parameters. */
        float    mass;               /**< Mass override. */
        uint32_t collision_group;    /**< Collision group index. */
        uint32_t ccd_enabled;        /**< CCD flag. */
        uint32_t hull_offset;        /**< Index into hull_verts. */
        uint32_t hull_count;         /**< Vertex count for this bone's hull. */
    } bones[PREFAB_MAX_BONES];

    float    hull_verts[PREFAB_MAX_HULL_VERTS * 3]; /**< Hull vertex data. */
    uint32_t hull_vert_count;        /**< Total hull vertices used. */

    /* Optional per-bone rest_local overrides (stored in fpfab, NOT in fskel).
     * When bone_pose_count > 0, bone_rest_local[i] contains the 4x4 rest_local
     * matrix for bone i.  Only bones 0..bone_pose_count-1 are valid. */
    uint32_t bone_pose_count;        /**< Number of bones with pose overrides. */
    float    bone_rest_local[PREFAB_MAX_BONES][16]; /**< Per-bone rest_local matrices. */
} prefab_def_t;

/**
 * @brief Initialize a prefab definition to empty.
 * @param def  Definition to initialize (non-NULL).
 */
void prefab_def_init(prefab_def_t *def);

/**
 * @brief Clear a prefab definition to empty (same as init).
 * @param def  Definition to clear (non-NULL).
 */
void prefab_def_clear(prefab_def_t *def);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_DEF_H */
