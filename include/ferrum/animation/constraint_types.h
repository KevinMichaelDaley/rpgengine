/**
 * @file constraint_types.h
 * @brief Unified constraint type enum and coordinate space enum.
 *
 * These types are shared by both the animation pose solver and the
 * physics rigid-body engine. A constraint_type_t value identifies
 * a constraint regardless of whether it operates on bones or bodies.
 *
 * This header defines exactly 2 public types:
 *   - constraint_type_t  (enum of 20 Blender-compatible constraint types)
 *   - constraint_space_t (coordinate space for constraint evaluation)
 */

#ifndef FERRUM_ANIMATION_CONSTRAINT_TYPES_H
#define FERRUM_ANIMATION_CONSTRAINT_TYPES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief All 20 Blender-compatible constraint types.
 *
 * These are grouped by category but numbered sequentially.
 * Both the animation constraint solver and the physics engine
 * consume these values.
 */
typedef enum constraint_type {
    /* IK / Chain */
    CONSTRAINT_IK              = 0,   /**< Inverse Kinematics (CCD/FABRIK). */
    CONSTRAINT_SPLINE_IK       = 1,   /**< Spline IK along a curve. */

    /* Parenting */
    CONSTRAINT_CHILD_OF        = 2,   /**< Dynamic re-parenting. */

    /* Transform Copy */
    CONSTRAINT_COPY_TRANSFORMS = 3,   /**< Copy full TRS from target. */
    CONSTRAINT_COPY_ROTATION   = 4,   /**< Copy rotation from target. */
    CONSTRAINT_COPY_LOCATION   = 5,   /**< Copy location from target. */
    CONSTRAINT_COPY_SCALE      = 6,   /**< Copy scale from target. */

    /* Tracking */
    CONSTRAINT_DAMPED_TRACK    = 7,   /**< Smallest rotation to aim at target. */
    CONSTRAINT_TRACK_TO        = 8,   /**< Track target with up axis. */
    CONSTRAINT_LOCKED_TRACK    = 9,   /**< Track target around locked axis. */

    /* Limits */
    CONSTRAINT_LIMIT_ROTATION  = 10,  /**< Clamp rotation to range per axis. */
    CONSTRAINT_LIMIT_LOCATION  = 11,  /**< Clamp position to bounds per axis. */
    CONSTRAINT_LIMIT_SCALE     = 12,  /**< Clamp scale to range per axis. */

    /* Transform Mapping */
    CONSTRAINT_TRANSFORMATION  = 13,  /**< Map one channel to another. */
    CONSTRAINT_ACTION          = 14,  /**< Drive animation clip from target. */

    /* Surface / Volume */
    CONSTRAINT_CLAMP_TO        = 15,  /**< Restrict to curve. */
    CONSTRAINT_FLOOR           = 16,  /**< Half-space boundary. */
    CONSTRAINT_MAINTAIN_VOLUME = 17,  /**< Volume preservation on scale. */
    CONSTRAINT_SHRINKWRAP      = 18,  /**< Project onto mesh surface. */

    /* Pivot */
    CONSTRAINT_PIVOT           = 19,  /**< Change rotation pivot point. */

    CONSTRAINT_TYPE_COUNT      = 20   /**< Total number of constraint types. */
} constraint_type_t;

/**
 * @brief Coordinate space for constraint evaluation.
 *
 * Determines how owner and target transforms are interpreted
 * during constraint solving. Used by both animation and physics.
 */
typedef enum constraint_space {
    CONSTRAINT_SPACE_WORLD = 0,  /**< World space (absolute). */
    CONSTRAINT_SPACE_LOCAL = 1,  /**< Relative to parent bone/body. */
    CONSTRAINT_SPACE_POSE  = 2,  /**< Relative to rest pose. */
    CONSTRAINT_SPACE_BONE  = 3,  /**< Relative to bone's own rest transform. */
} constraint_space_t;

/**
 * @brief Axis selector for tracking and limit constraints.
 */
typedef enum constraint_axis {
    CONSTRAINT_AXIS_X     = 0,  /**< Positive X axis. */
    CONSTRAINT_AXIS_Y     = 1,  /**< Positive Y axis. */
    CONSTRAINT_AXIS_Z     = 2,  /**< Positive Z axis. */
    CONSTRAINT_AXIS_NEG_X = 3,  /**< Negative X axis. */
    CONSTRAINT_AXIS_NEG_Y = 4,  /**< Negative Y axis. */
    CONSTRAINT_AXIS_NEG_Z = 5,  /**< Negative Z axis. */
} constraint_axis_t;

/**
 * @brief Mix mode for Copy Transforms / Copy Rotation.
 */
typedef enum constraint_mix_mode {
    CONSTRAINT_MIX_REPLACE      = 0, /**< Replace owner with target. */
    CONSTRAINT_MIX_BEFORE       = 1, /**< Apply target before owner. */
    CONSTRAINT_MIX_AFTER        = 2, /**< Apply target after owner. */
    CONSTRAINT_MIX_BEFORE_FULL  = 3, /**< Full matrix multiply before. */
    CONSTRAINT_MIX_AFTER_FULL   = 4, /**< Full matrix multiply after. */
} constraint_mix_mode_t;

/**
 * @brief Transform channel selector for Transformation constraint.
 */
typedef enum constraint_channel {
    CONSTRAINT_CHANNEL_LOC_X = 0,
    CONSTRAINT_CHANNEL_LOC_Y = 1,
    CONSTRAINT_CHANNEL_LOC_Z = 2,
    CONSTRAINT_CHANNEL_ROT_X = 3,
    CONSTRAINT_CHANNEL_ROT_Y = 4,
    CONSTRAINT_CHANNEL_ROT_Z = 5,
    CONSTRAINT_CHANNEL_SCL_X = 6,
    CONSTRAINT_CHANNEL_SCL_Y = 7,
    CONSTRAINT_CHANNEL_SCL_Z = 8,
} constraint_channel_t;

/**
 * @brief Floor constraint location (which side is "below").
 */
typedef enum constraint_floor_location {
    CONSTRAINT_FLOOR_BELOW_NEG_Y = 0, /**< Floor below -Y. */
    CONSTRAINT_FLOOR_BELOW_NEG_X = 1, /**< Floor below -X. */
    CONSTRAINT_FLOOR_BELOW_NEG_Z = 2, /**< Floor below -Z. */
} constraint_floor_location_t;

/**
 * @brief Shrinkwrap projection mode.
 */
typedef enum constraint_shrinkwrap_mode {
    CONSTRAINT_SHRINKWRAP_NEAREST_SURFACE = 0,
    CONSTRAINT_SHRINKWRAP_PROJECT         = 1,
    CONSTRAINT_SHRINKWRAP_NEAREST_VERTEX  = 2,
} constraint_shrinkwrap_mode_t;

/**
 * @brief Get human-readable name for a constraint type.
 * @param type The constraint type.
 * @return Constant string name, or "Unknown" for invalid types.
 *         Returned pointer is valid for the lifetime of the program.
 */
const char *constraint_type_name(constraint_type_t type);

/**
 * @brief Check if a constraint type value is valid.
 * @param type The constraint type to validate.
 * @return true if type is in [0, CONSTRAINT_TYPE_COUNT).
 */
bool constraint_type_is_valid(constraint_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_CONSTRAINT_TYPES_H */
