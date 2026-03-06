#ifndef FERRUM_PHYSICS_JOINT_H
#define FERRUM_PHYSICS_JOINT_H

/** @file
 * @brief Joint constraint types and row builders.
 *
 * Defines distance, ball, and hinge joints.  Each joint stores its
 * configuration (anchors, axis, spring parameters) and an output
 * array of Jacobian rows compatible with the existing TGS/XPBD solver.
 *
 * Usage:
 *   1. phys_joint_init() to zero-fill with safe defaults.
 *   2. Set type, body indices, anchors, and type-specific params.
 *   3. Call the matching build function each frame before solving.
 *   4. Feed j.rows[0..j.row_count-1] into the iterative solver.
 */

#include <stdint.h>

#include "ferrum/physics/phys_types.h"
#include "ferrum/physics/constraint.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
struct phys_body;

/** Maximum Jacobian rows a single joint can produce (hinge = 5). */
#define PHYS_JOINT_MAX_ROWS 6

/**
 * @brief Joint type discriminator.
 */
typedef enum phys_joint_type {
    PHYS_JOINT_DISTANCE       = 0,  /**< Spring-damper distance constraint. */
    PHYS_JOINT_BALL           = 1,  /**< 3-DOF rotation (positional lock). */
    PHYS_JOINT_HINGE          = 2,  /**< 1-DOF rotation (positional + angular lock). */
    PHYS_JOINT_LOCK           = 3,  /**< 0-DOF full rigid attachment (3 pos + 3 ang). */
    PHYS_JOINT_COPY_ROTATION  = 4,  /**< Match orientation (3 angular rows). */
    PHYS_JOINT_LIMIT_ROTATION = 5,  /**< Per-axis angular limits (up to 3 clamped rows). */
    PHYS_JOINT_LIMIT_POSITION = 6,  /**< Per-axis positional limits (up to 3 clamped rows). */
    PHYS_JOINT_AIM            = 7,  /**< Align axis toward target (2 angular rows). */
    PHYS_JOINT_IK             = 8,  /**< IK chain pair: angular rows toward target (3 angular rows). */
} phys_joint_type_t;

/**
 * @brief Joint descriptor and solver output.
 *
 * Combines configuration (type, anchors, parameters) with the
 * Jacobian rows produced by the build functions.
 *
 * @par Ownership
 * Caller owns the struct.  No internal allocations.
 *
 * @par Thread safety
 * Not thread-safe — each joint should be built by one thread.
 */
typedef struct phys_joint {
    phys_joint_type_t type;     /**< Joint type discriminator. */
    uint32_t body_a;            /**< Index of body A (UINT32_MAX = unset). */
    uint32_t body_b;            /**< Index of body B (UINT32_MAX = unset). */

    phys_vec3_t local_anchor_a; /**< Anchor point in body A's local space. */
    phys_vec3_t local_anchor_b; /**< Anchor point in body B's local space. */
    phys_vec3_t local_axis_a;   /**< Hinge axis in body A's local space. */

    /* Distance joint parameters. */
    float rest_length;          /**< Desired distance between anchors. */
    float stiffness;            /**< Spring stiffness (0 = rigid). */
    float damping;              /**< Damping coefficient. */

    /* Per-axis limit parameters (limit_rotation / limit_position). */
    float limit_min[3];         /**< Min angle (rad) or position per axis. */
    float limit_max[3];         /**< Max angle (rad) or position per axis. */
    uint8_t limit_axes;         /**< Bitmask: bit 0=X, 1=Y, 2=Z active. */

    /* Aim joint: which local axis of body_b to align toward body_a. */
    phys_vec3_t track_axis;     /**< Local axis on body_b to aim (e.g. {0,1,0}). */

    /* IK chain pair (PHYS_JOINT_IK).
     * Each IK pair connects two consecutive chain bodies.  The build
     * function computes angular rows that steer both bodies to reduce
     * the end-effector → target position error. */
    uint32_t    ik_ee_body;     /**< Body index of end-effector (chain tip). */
    uint32_t    ik_target_body; /**< Body index of IK target (read position dynamically).
                                 *   UINT32_MAX = use static ik_target_pos instead. */
    phys_vec3_t ik_target_pos;  /**< Fallback world-space IK target position
                                 *   (only used when ik_target_body == UINT32_MAX). */

    /* Warmstarting: cached accumulated impulses from previous substep.
     * Seeded into rows at build time, written back after solve. */
    float cached_lambda[PHYS_JOINT_MAX_ROWS]; /**< Cached lambda per row. */

    /* Solver output (populated by build functions). */
    uint8_t row_count;          /**< Number of active rows after build. */
    phys_jacobian_row_t rows[PHYS_JOINT_MAX_ROWS]; /**< Jacobian rows. */
} phys_joint_t;

/**
 * @brief Initialize a joint to safe defaults.
 *
 * Zeroes the struct, sets body indices to UINT32_MAX, type to DISTANCE.
 *
 * @param joint  Joint to initialize.  NULL is a no-op.
 *
 * @par Side effects: none beyond writing to *joint.
 */
void phys_joint_init(phys_joint_t *joint);

/**
 * @brief Build constraint rows for a distance joint.
 *
 * Produces 1 Jacobian row along the anchor separation axis.
 * Bias corrects the difference between current distance and rest_length.
 * Lambda bounds are bilateral (can push and pull).
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_DISTANCE).
 *                If NULL, no-op.
 * @param body_a  Pointer to body A.  If NULL, no-op.
 * @param body_b  Pointer to body B.  If NULL, no-op.
 * @param dt      Timestep in seconds.  Must be > 0; if <= 0, no-op
 *                (row_count stays 0).
 *
 * @par Ownership: caller owns all pointers.  No allocations.
 * @par Side effects: writes joint->rows and joint->row_count.
 */
void phys_joint_build_distance(phys_joint_t *joint,
                               const struct phys_body *body_a,
                               const struct phys_body *body_b,
                               float dt);

/**
 * @brief Build constraint rows for a ball (spherical) joint.
 *
 * Produces 3 Jacobian rows locking the anchor points along X, Y, Z.
 * Bias corrects positional error between world-space anchors.
 * Lambda bounds are bilateral.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_BALL).
 *                If NULL, no-op.
 * @param body_a  Pointer to body A.  If NULL, no-op.
 * @param body_b  Pointer to body B.  If NULL, no-op.
 * @param dt      Timestep in seconds.  Must be > 0; if <= 0, no-op.
 *
 * @par Ownership: caller owns all pointers.  No allocations.
 * @par Side effects: writes joint->rows and joint->row_count.
 */
void phys_joint_build_ball(phys_joint_t *joint,
                           const struct phys_body *body_a,
                           const struct phys_body *body_b,
                           float dt);

/**
 * @brief Build constraint rows for a hinge (revolute) joint.
 *
 * Produces 5 Jacobian rows:
 *   - Rows 0–2: positional lock (same as ball joint).
 *   - Rows 3–4: angular lock perpendicular to the hinge axis,
 *     preventing rotation around any axis other than the hinge.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_HINGE).
 *                If NULL, no-op.
 * @param body_a  Pointer to body A.  If NULL, no-op.
 * @param body_b  Pointer to body B.  If NULL, no-op.
 * @param dt      Timestep in seconds.  Must be > 0; if <= 0, no-op.
 *
 * @par Ownership: caller owns all pointers.  No allocations.
 * @par Side effects: writes joint->rows and joint->row_count.
 */
void phys_joint_build_hinge(phys_joint_t *joint,
                            const struct phys_body *body_a,
                            const struct phys_body *body_b,
                            float dt);

/**
 * @brief Build constraint rows for a lock (rigid attachment) joint.
 *
 * Produces 6 Jacobian rows: 3 positional (like ball) + 3 angular
 * (lock all rotation axes).  Used for Copy Transforms / Child Of.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_LOCK).
 * @param body_a  Pointer to body A.
 * @param body_b  Pointer to body B.
 * @param dt      Timestep in seconds.
 */
void phys_joint_build_lock(phys_joint_t *joint,
                           const struct phys_body *body_a,
                           const struct phys_body *body_b,
                           float dt);

/**
 * @brief Build constraint rows for copy-rotation joint.
 *
 * Produces 3 angular Jacobian rows that drive body_b's orientation
 * to match body_a's orientation.  No positional constraint.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_COPY_ROTATION).
 * @param body_a  Pointer to body A (orientation source).
 * @param body_b  Pointer to body B (constrained body).
 * @param dt      Timestep in seconds.
 */
void phys_joint_build_copy_rotation(phys_joint_t *joint,
                                    const struct phys_body *body_a,
                                    const struct phys_body *body_b,
                                    float dt);

/**
 * @brief Build constraint rows for angular limits.
 *
 * Produces up to 3 clamped angular rows, one per axis enabled in
 * limit_axes.  Each row is one-sided: active only when the relative
 * angle exceeds limit_min or limit_max.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_LIMIT_ROTATION).
 * @param body_a  Pointer to body A (reference frame).
 * @param body_b  Pointer to body B (constrained body).
 * @param dt      Timestep in seconds.
 */
void phys_joint_build_limit_rotation(phys_joint_t *joint,
                                     const struct phys_body *body_a,
                                     const struct phys_body *body_b,
                                     float dt);

/**
 * @brief Build constraint rows for positional limits.
 *
 * Produces up to 3 clamped positional rows, one per axis enabled in
 * limit_axes.  Each row is one-sided: active only when the position
 * exceeds limit_min or limit_max on that axis.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_LIMIT_POSITION).
 * @param body_a  Pointer to body A (reference frame).
 * @param body_b  Pointer to body B (constrained body).
 * @param dt      Timestep in seconds.
 */
void phys_joint_build_limit_position(phys_joint_t *joint,
                                     const struct phys_body *body_a,
                                     const struct phys_body *body_b,
                                     float dt);

/**
 * @brief Build constraint rows for an aim (track-to) joint.
 *
 * Produces 2 angular rows that align body_b's track_axis toward
 * body_a's position.  Similar to Damped Track / Track To constraints.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_AIM).
 * @param body_a  Pointer to body A (target position).
 * @param body_b  Pointer to body B (aiming body).
 * @param dt      Timestep in seconds.
 */
void phys_joint_build_aim(phys_joint_t *joint,
                          const struct phys_body *body_a,
                          const struct phys_body *body_b,
                          float dt);

/**
 * @brief Build constraint rows for an IK chain pair.
 *
 * Produces 3 angular Jacobian rows that drive body_a and body_b to
 * rotate such that the end-effector body reaches ik_target_pos.
 * Each row corresponds to one world axis (X, Y, Z).
 *
 * The lever arm from each body to the end-effector determines the
 * per-body angular Jacobian contribution.  Bodies far from the
 * end-effector have larger lever arms and produce proportionally
 * larger corrections.
 *
 * @param joint   Joint descriptor (type should be PHYS_JOINT_IK).
 *                ik_ee_body and ik_target_pos must be set.
 * @param body_a  Pointer to body A (upstream in chain).
 * @param body_b  Pointer to body B (downstream in chain).
 * @param ee_body Pointer to end-effector body (chain tip).
 * @param dt      Timestep in seconds.
 */
void phys_joint_build_ik(phys_joint_t *joint,
                         const struct phys_body *body_a,
                         const struct phys_body *body_b,
                         const struct phys_body *ee_body,
                         float dt);

/* Forward declaration for constraint output. */
struct phys_constraint;

/**
 * @brief Convert a built joint into solver-compatible constraint(s).
 *
 * Call one of the build functions first to populate joint->rows and
 * joint->row_count.  This function then packs those rows into one or
 * two phys_constraint_t entries (max 3 rows each).
 *
 * - Distance joint (1 row)          → 1 constraint.
 * - Ball joint (3 rows)             → 1 constraint.
 * - Copy rotation (3 rows)          → 1 constraint.
 * - Hinge joint (5 rows)            → 2 constraints (3 + 2).
 * - Lock joint (6 rows)             → 2 constraints (3 + 3).
 * - Limit rotation (up to 3 rows)   → 1 constraint.
 * - Limit position (up to 3 rows)   → 1 constraint.
 * - Aim joint (2 rows)              → 1 constraint.
 *
 * Each output constraint has is_joint=1, friction=0, penetration=0,
 * and bilateral lambda bounds preserved from the build step.
 *
 * @param joint          Built joint (row_count > 0).  If NULL, returns 0.
 * @param out            Output constraint array.  Must have capacity
 *                       for at least 2 entries.  If NULL, returns 0.
 * @param max_out        Capacity of the output array.
 * @param solver_mode    Solver mode to assign (0=TGS, 1=XPBD).
 * @return Number of constraints written (0, 1, or 2).
 *
 * @par Ownership: caller owns all pointers.  No allocations.
 * @par Side effects: writes to out[0] and possibly out[1].
 */
uint32_t phys_joint_build_constraints(const phys_joint_t *joint,
                                      struct phys_constraint *out,
                                      uint32_t max_out,
                                      uint8_t solver_mode);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_JOINT_H */
