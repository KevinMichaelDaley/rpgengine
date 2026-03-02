#ifndef FERRUM_PHYSICS_POSITION_PROJECTION_H
#define FERRUM_PHYSICS_POSITION_PROJECTION_H

/** @file
 * @brief Sparse per-island position projection (replaces Baumgarte).
 *
 * Solves A * lambda = -Phi(q) per island where A = J M^-1 J^T,
 * using the full block-diagonal Jacobian (linear + angular).
 * Applies generalized position corrections:
 *   delta_q.linear  = M^-1   * J_v^T * lambda  (translation)
 *   delta_q.angular = I^-1   * J_w^T * lambda  (orientation)
 *
 * Only operates on normal constraint rows (row 0 of each constraint).
 * Friction rows are unaffected.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_island;
struct phys_constraint;
struct phys_body;
struct phys_frame_arena;
struct phys_velocity;

struct phys_mat3;

/**
 * @brief Output of position projection for a single island.
 *
 * The correction_deltas array stores per-body generalized corrections:
 *   .linear  = translational position delta
 *   .angular = angular (pseudo-vector) orientation delta
 *
 * Ownership: arrays are arena-allocated and valid until arena reset.
 */
typedef struct phys_position_projection_result {
    bool success;                        /**< True if projection succeeded. */
    struct phys_velocity *correction_deltas; /**< Per-body generalized correction (linear + angular). */
} phys_position_projection_result_t;

/**
 * @brief Arguments for per-island position projection.
 *
 * @par Ownership
 * - island, constraints, bodies: borrowed, read-only.
 * - arena: borrowed, allocations made for output arrays.
 * - result: caller-owned output struct.
 * - shared_deltas: optional pre-allocated phys_velocity_t array (sized to
 *   body_count) to avoid per-island arena allocation.  If NULL, the
 *   function allocates from the arena.
 */
typedef struct phys_position_projection_args {
    const struct phys_island *island;    /**< Island to project. */
    const struct phys_constraint *constraints; /**< Global constraint array. */
    const struct phys_body *bodies;      /**< Global body array. */
    const struct phys_mat3 *inv_inertia_world; /**< World-space inverse inertia per body. */
    uint32_t body_count;                 /**< Total body count (for output array sizing). */
    float dt;                            /**< Timestep in seconds. */
    float slop;                          /**< Penetration slop (no correction below this). */
    struct phys_frame_arena *arena;      /**< Arena for output allocations. */
    phys_position_projection_result_t *result; /**< Output. */
    struct phys_velocity *shared_deltas; /**< Optional shared generalized delta array. */
} phys_position_projection_args_t;

/**
 * @brief Run position projection on a single island.
 *
 * For each normal constraint row, extracts the penetration depth from
 * the constraint bias, assembles the per-island Jacobian and Phi vector,
 * forms the Schur complement A = J M^-1 J^T, solves via dense LDL^T,
 * and computes position corrections and velocity deltas.
 *
 * @param args  Projection arguments. NULL-safe (no-op).
 *
 * @note Only normal rows (row 0) participate. Friction rows are ignored.
 * @note For sleeping islands, produces zero corrections and returns success.
 * @note Allocates from args->arena. No heap allocations.
 */
void phys_position_projection(const phys_position_projection_args_t *args);

/**
 * @brief Solve a dense symmetric positive-definite system A*x = b
 *        using in-place LDL^T factorization.
 *
 * @param A  Dense NxN matrix (row-major). Modified in place during solve.
 * @param b  Right-hand side vector of length N. Modified in place.
 * @param x  Output solution vector of length N.
 * @param n  System size.
 * @return true on success, false if matrix is singular or n==0.
 *
 * @note No heap allocations. Uses A and b as scratch space.
 * @note Degenerate (zero diagonal) rows are treated as zero lambda.
 */
bool phys_dense_ldlt_solve(float *A, float *b, float *x, uint32_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_POSITION_PROJECTION_H */
