/**
 * @file constraint_params.h
 * @brief Constraint parameter structs, tagged union, and skeleton definition.
 *
 * This header defines exactly 2 public types:
 *   - constraint_def_t  (tagged union holding one constraint with parameters)
 *   - skeleton_def_t    (joint hierarchy + per-joint constraint stacks)
 *
 * All per-type parameter structs are internal to the tagged union.
 * No dependency on physics or renderer headers — pure data types.
 */

#ifndef FERRUM_ANIMATION_CONSTRAINT_PARAMS_H
#define FERRUM_ANIMATION_CONSTRAINT_PARAMS_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/mat4.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/bone_collider.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of a joint name in skeleton_def_t. */
#define SKELETON_JOINT_NAME_MAX 64

/* ── Per-type parameter structs ──────────────────────────────────── */

/** @brief IK constraint parameters. */
typedef struct constraint_ik_params {
    uint32_t chain_length;     /**< Number of bones in chain (tip to root). */
    uint32_t pole_target_idx;  /**< Bone index for preferred bend plane, UINT32_MAX if none. */
    uint32_t iterations;       /**< Max solver iterations. */
    float    weight;           /**< IK weight (0.0–1.0). */
    float    orient_weight;    /**< Orientation matching weight (0.0–1.0). */
    bool     use_tail;         /**< Use bone tail as end-effector. */
} constraint_ik_params_t;

/** @brief Spline IK constraint parameters. */
typedef struct constraint_spline_ik_params {
    uint32_t chain_length;              /**< Number of bones in chain. */
    float    control_points[16 * 3];    /**< Up to 16 spline control points (x,y,z). */
    uint32_t control_point_count;       /**< Number of active control points. */
    constraint_axis_t twist_axis;       /**< Twist distribution axis. */
} constraint_spline_ik_params_t;

/** @brief Child Of constraint parameters. */
typedef struct constraint_child_of_params {
    bool use_location_x, use_location_y, use_location_z;
    bool use_rotation_x, use_rotation_y, use_rotation_z;
    bool use_scale_x,    use_scale_y,    use_scale_z;
    mat4_t inverse_matrix;  /**< Cached inverse of target at "set inverse" time. */
} constraint_child_of_params_t;

/** @brief Copy Transforms constraint parameters. */
typedef struct constraint_copy_transforms_params {
    constraint_mix_mode_t mix_mode;
} constraint_copy_transforms_params_t;

/** @brief Copy Rotation constraint parameters. */
typedef struct constraint_copy_rotation_params {
    constraint_mix_mode_t mix_mode;
    bool use_x, use_y, use_z;
    bool invert_x, invert_y, invert_z;
} constraint_copy_rotation_params_t;

/** @brief Copy Location constraint parameters. */
typedef struct constraint_copy_location_params {
    bool use_x, use_y, use_z;
    bool invert_x, invert_y, invert_z;
    bool offset;  /**< Additive mode (add target to owner instead of replace). */
} constraint_copy_location_params_t;

/** @brief Copy Scale constraint parameters. */
typedef struct constraint_copy_scale_params {
    bool  use_x, use_y, use_z;
    float power;   /**< Exponent applied to copied scale (default 1.0). */
    bool  offset;  /**< Additive scale mode. */
} constraint_copy_scale_params_t;

/** @brief Damped Track constraint parameters. */
typedef struct constraint_damped_track_params {
    constraint_axis_t track_axis;  /**< Which axis to point at target. */
} constraint_damped_track_params_t;

/** @brief Track To constraint parameters. */
typedef struct constraint_track_to_params {
    constraint_axis_t track_axis;  /**< Axis to point at target. */
    constraint_axis_t up_axis;     /**< Axis to keep aligned to world up. */
} constraint_track_to_params_t;

/** @brief Locked Track constraint parameters. */
typedef struct constraint_locked_track_params {
    constraint_axis_t track_axis;  /**< Axis to point at target. */
    constraint_axis_t lock_axis;   /**< Axis that must not change. */
} constraint_locked_track_params_t;

/** @brief Limit Rotation constraint parameters. */
typedef struct constraint_limit_rotation_params {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
    bool  use_limit_x, use_limit_y, use_limit_z;
} constraint_limit_rotation_params_t;

/** @brief Limit Location constraint parameters. */
typedef struct constraint_limit_location_params {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
    bool  use_min_x, use_max_x;
    bool  use_min_y, use_max_y;
    bool  use_min_z, use_max_z;
} constraint_limit_location_params_t;

/** @brief Limit Scale constraint parameters. */
typedef struct constraint_limit_scale_params {
    float min_x, max_x;
    float min_y, max_y;
    float min_z, max_z;
    bool  use_min_x, use_max_x;
    bool  use_min_y, use_max_y;
    bool  use_min_z, use_max_z;
} constraint_limit_scale_params_t;

/** @brief Transformation mapping constraint parameters. */
typedef struct constraint_transformation_params {
    constraint_channel_t from_channel;  /**< Source transform channel. */
    constraint_channel_t to_channel;    /**< Destination transform channel. */
    float from_min, from_max;           /**< Source range. */
    float to_min, to_max;               /**< Destination range. */
    bool  extrapolate;                  /**< Allow extrapolation outside range. */
} constraint_transformation_params_t;

/** @brief Action constraint parameters. */
typedef struct constraint_action_params {
    uint32_t             action_clip_idx;     /**< Index into animation clip array. */
    constraint_channel_t transform_channel;   /**< Which target channel drives playback. */
    float                min_value, max_value; /**< Target channel range → action time. */
} constraint_action_params_t;

/** @brief Clamp To constraint parameters. */
typedef struct constraint_clamp_to_params {
    constraint_axis_t main_axis;            /**< Primary movement axis along curve. */
    float             control_points[16*3]; /**< Up to 16 curve control points. */
    uint32_t          control_point_count;
    bool              cyclic;               /**< Wrap-around curve. */
} constraint_clamp_to_params_t;

/** @brief Floor constraint parameters. */
typedef struct constraint_floor_params {
    float                        offset;          /**< Distance above floor plane. */
    bool                         use_rotation;    /**< Floor rotates with target. */
    constraint_floor_location_t  floor_location;  /**< Which side is "below". */
} constraint_floor_params_t;

/** @brief Maintain Volume constraint parameters. */
typedef struct constraint_maintain_volume_params {
    constraint_axis_t free_axis;  /**< Axis that scales freely. */
    float             volume;     /**< Reference volume (default 1.0). */
} constraint_maintain_volume_params_t;

/** @brief Shrinkwrap constraint parameters. */
typedef struct constraint_shrinkwrap_params {
    constraint_shrinkwrap_mode_t shrinkwrap_type;
    float                        distance;  /**< Offset from surface. */
} constraint_shrinkwrap_params_t;

/** @brief Pivot constraint parameters. */
typedef struct constraint_pivot_params {
    float offset[3];         /**< Pivot offset (x, y, z). */
    float rotation_range;    /**< Only active when rotation exceeds threshold. */
} constraint_pivot_params_t;

/* ── Tagged union ────────────────────────────────────────────────── */

/**
 * @brief A single constraint definition with typed parameters.
 *
 * Used by both the animation pose constraint solver and the physics
 * engine's motor/joint system. The `type` field selects which member
 * of the `params` union is active.
 *
 * @note This struct has no ownership — it is pure data. Copy freely.
 * @note target_bone_idx = UINT32_MAX means no bone target (external target).
 */
typedef struct constraint_def {
    constraint_type_t  type;             /**< Which constraint this is. */
    float              influence;        /**< Blend factor 0.0–1.0. */
    constraint_space_t owner_space;      /**< Space for owner evaluation. */
    constraint_space_t target_space;     /**< Space for target evaluation. */
    uint32_t           target_bone_idx;  /**< Target bone, or UINT32_MAX. */

    /** Per-type parameters (only the member matching `type` is valid). */
    union {
        constraint_ik_params_t              ik;
        constraint_spline_ik_params_t       spline_ik;
        constraint_child_of_params_t        child_of;
        constraint_copy_transforms_params_t copy_transforms;
        constraint_copy_rotation_params_t   copy_rotation;
        constraint_copy_location_params_t   copy_location;
        constraint_copy_scale_params_t      copy_scale;
        constraint_damped_track_params_t    damped_track;
        constraint_track_to_params_t        track_to;
        constraint_locked_track_params_t    locked_track;
        constraint_limit_rotation_params_t  limit_rotation;
        constraint_limit_location_params_t  limit_location;
        constraint_limit_scale_params_t     limit_scale;
        constraint_transformation_params_t  transformation;
        constraint_action_params_t          action;
        constraint_clamp_to_params_t        clamp_to;
        constraint_floor_params_t           floor;
        constraint_maintain_volume_params_t maintain_volume;
        constraint_shrinkwrap_params_t      shrinkwrap;
        constraint_pivot_params_t           pivot;
    } params;
} constraint_def_t;

/* ── Skeleton definition ─────────────────────────────────────────── */

/**
 * @brief Complete skeleton definition with joint hierarchy and constraints.
 *
 * Holds joint names, parent-child relationships, rest transforms,
 * and per-joint constraint stacks. This is the runtime representation
 * loaded from .fskel files and consumed by both the animation solver
 * and the physics ragdoll builder.
 *
 * Memory layout:
 * - joint_names:       joint_count × char[SKELETON_JOINT_NAME_MAX]
 * - parent_indices:    joint_count × uint32_t
 * - rest_local:        joint_count × mat4_t (local rest transforms)
 * - rest_world:        joint_count × mat4_t (world rest transforms)
 * - constraint_counts: joint_count × uint32_t
 * - constraints:       joint_count × max_constraints_per_joint × constraint_def_t
 *
 * All arrays are allocated in a single block by skeleton_def_init().
 *
 * @note Owns all memory. Call skeleton_def_destroy() to free.
 */
typedef struct skeleton_def {
    uint32_t joint_count;               /**< Number of joints. */
    uint32_t max_constraints_per_joint; /**< Constraint stack capacity per joint. */

    char           (*joint_names)[SKELETON_JOINT_NAME_MAX]; /**< Joint name strings. */
    uint32_t       *parent_indices;     /**< Parent joint index per joint (UINT32_MAX = root). */
    mat4_t         *rest_local;         /**< Local rest transforms. */
    mat4_t         *rest_world;         /**< World rest transforms. */
    uint32_t       *constraint_counts;  /**< Number of active constraints per joint. */
    constraint_def_t *constraints;      /**< Flat array: [joint][constraint_idx]. */

    /* Per-bone collision descriptors (fskel v2 COLL chunk).
     * NULL if the file is v1 or no collision data was provided. */
    bone_collider_desc_t *colliders;    /**< One per joint, or NULL. */
    float          *hull_vertices;      /**< Convex hull vertex data (x,y,z triples). */
    uint32_t        hull_vertex_count;  /**< Total hull vertices across all bones. */
} skeleton_def_t;

/**
 * @brief Initialize a skeleton definition with the given capacity.
 *
 * Allocates all internal arrays. Parent indices are initialized to
 * UINT32_MAX (root). Constraint counts are initialized to 0.
 * Rest transforms are initialized to identity.
 *
 * @param skel              Output skeleton (non-NULL).
 * @param joint_count       Number of joints (must be >= 1).
 * @param max_constraints   Max constraints per joint.
 * @return true on success, false on invalid args or allocation failure.
 *
 * @note Caller owns the skeleton. Call skeleton_def_destroy() to free.
 */
bool skeleton_def_init(skeleton_def_t *skel, uint32_t joint_count,
                       uint32_t max_constraints);

/**
 * @brief Free all memory owned by a skeleton definition.
 *
 * Safe to call on a zeroed or already-destroyed skeleton.
 *
 * @param skel Skeleton to destroy (non-NULL, but internal pointers may be NULL).
 */
void skeleton_def_destroy(skeleton_def_t *skel);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_CONSTRAINT_PARAMS_H */
