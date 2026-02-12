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
    PHYS_JOINT_DISTANCE = 0,  /**< Spring-damper distance constraint. */
    PHYS_JOINT_BALL     = 1,  /**< 3-DOF rotation (positional lock). */
    PHYS_JOINT_HINGE    = 2,  /**< 1-DOF rotation (positional + angular lock). */
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_JOINT_H */
