#ifndef FERRUM_PHYSICS_CONSTRAINT_H
#define FERRUM_PHYSICS_CONSTRAINT_H

/** @file
 * @brief Constraint and Jacobian structures for the TGS contact solver.
 *
 * Each contact point generates up to 3 constraint rows: 1 normal
 * (non-penetration) and 2 friction (tangential).
 */

#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations to avoid circular dependencies. */
struct phys_body;
struct phys_contact_point;

/**
 * @brief A single constraint row (Jacobian + solver state).
 *
 * Stores the 12-DOF Jacobian (linear + angular for both bodies),
 * precomputed effective mass, Baumgarte bias, accumulated impulse,
 * and clamp limits.
 */
typedef struct phys_jacobian_row {
    phys_vec3_t J_va;       /**< Linear Jacobian for body A. */
    phys_vec3_t J_wa;       /**< Angular Jacobian for body A. */
    phys_vec3_t J_vb;       /**< Linear Jacobian for body B. */
    phys_vec3_t J_wb;       /**< Angular Jacobian for body B. */
    float effective_mass;   /**< 1 / (J * M^-1 * J^T). */
    float bias;             /**< Baumgarte stabilization + restitution bias. */
    float lambda;           /**< Accumulated impulse (warmstarted). */
    float lambda_min;       /**< Clamp minimum (0 for normal, -friction*big for tangent). */
    float lambda_max;       /**< Clamp maximum (+big for normal, +friction*big for tangent). */
    float pseudo_lambda;    /**< Accumulated split-impulse (position correction, not warmstarted). */
} phys_jacobian_row_t;

/** Maximum constraint rows per contact (1 normal + 2 friction). */
#define PHYS_MAX_CONSTRAINT_ROWS 3

/**
 * @brief A constraint grouping up to 3 Jacobian rows.
 *
 * Used for both contact constraints (1 normal + 2 friction) and
 * joint constraints (positional or angular rows).
 */
typedef struct phys_constraint {
    uint32_t body_a;        /**< Index of body A. */
    uint32_t body_b;        /**< Index of body B. */
    uint32_t manifold_idx;  /**< Back-reference to manifold for warmstart writeback. */
    uint8_t point_idx;      /**< Which contact point in the manifold. */
    uint8_t row_count;      /**< Number of active rows (typically 3). */
    uint8_t solver_mode;    /**< phys_solver_mode_t: 0=TGS, 1=XPBD. */
    uint8_t is_joint;       /**< Non-zero for joint constraints (skip friction cone). */
    float friction;         /**< Combined friction coefficient for Coulomb cone. */
    float penetration;      /**< Raw penetration depth for position projection. */
    phys_jacobian_row_t rows[PHYS_MAX_CONSTRAINT_ROWS]; /**< Constraint rows. */
} phys_constraint_t;

/**
 * @brief Build a contact constraint from a contact point.
 *
 * Populates 3 rows: normal (row 0) and two friction tangent rows (rows 1, 2).
 * Computes Jacobians, Baumgarte bias, and effective mass for each row.
 *
 * @param c         Constraint to populate. If NULL, no-op.
 * @param body_a    Pointer to body A. If NULL, no-op.
 * @param body_b    Pointer to body B. If NULL, no-op.
 * @param contact   Contact point data. If NULL, no-op.
 * @param friction  Combined friction coefficient.
 * @param restitution Combined restitution coefficient.
 * @param dt        Timestep (seconds). Must be > 0.
 * @param baumgarte Baumgarte stabilization factor (typically 0.1–0.3).
 * @param slop      Penetration slop threshold (no correction below this).
 *
 * @note Ownership: caller owns all pointers. No allocations performed.
 * @note Side effects: none beyond writing to *c.
 */
void phys_constraint_build_contact(
    phys_constraint_t *c,
    const struct phys_body *body_a,
    const struct phys_body *body_b,
    const struct phys_contact_point *contact,
    float friction,
    float restitution,
    float dt,
    float baumgarte,
    float slop);

/**
 * @brief Compute an orthonormal tangent basis from a contact normal.
 *
 * Produces two tangent vectors orthogonal to each other and to the normal.
 *
 * @param normal    The contact normal (should be unit length).
 * @param tangent1  Output: first tangent direction. If NULL, no-op.
 * @param tangent2  Output: second tangent direction. If NULL, no-op.
 *
 * @note No allocations. No side effects beyond writing outputs.
 */
void phys_compute_tangent_basis(
    phys_vec3_t normal,
    phys_vec3_t *tangent1,
    phys_vec3_t *tangent2);

/**
 * @brief Compute effective mass for a single Jacobian row.
 *
 * effective_mass = 1 / (inv_mass_a + inv_mass_b
 *                       + dot(J_wa, diag(inv_inertia_a) * J_wa)
 *                       + dot(J_wb, diag(inv_inertia_b) * J_wb))
 *
 * @param row           Jacobian row. If NULL, returns 0.
 * @param inv_mass_a    Inverse mass of body A.
 * @param inv_inertia_a Diagonal inverse inertia of body A. If NULL, returns 0.
 * @param inv_mass_b    Inverse mass of body B.
 * @param inv_inertia_b Diagonal inverse inertia of body B. If NULL, returns 0.
 * @return Effective mass (always >= 0). Returns 0 on error.
 *
 * @note No allocations. No side effects.
 */
float phys_compute_effective_mass(
    const phys_jacobian_row_t *row,
    float inv_mass_a, const phys_vec3_t *inv_inertia_a,
    float inv_mass_b, const phys_vec3_t *inv_inertia_b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONSTRAINT_H */
