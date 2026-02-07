#ifndef FERRUM_PHYSICS_POSITION_PROJECTION_H
#define FERRUM_PHYSICS_POSITION_PROJECTION_H

/** @file
 * @brief Sparse per-island position projection (replaces Baumgarte).
 *
 * Solves A * lambda = -Phi(q) per island where A = J M^-1 J^T,
 * then applies position corrections delta_q = M^-1 J^T lambda
 * and synchronizes velocities v_delta = delta_q / dt.
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

/**
 * @brief Output of position projection for a single island.
 *
 * Ownership: arrays are arena-allocated and valid until arena reset.
 */
typedef struct phys_position_projection_result {
    bool success;                    /**< True if projection succeeded. */
    phys_vec3_t *position_deltas;    /**< Per-body position correction (indexed by body_index). */
    struct phys_velocity *velocity_deltas; /**< Per-body velocity correction (delta_q / dt). */
} phys_position_projection_result_t;

/**
 * @brief Arguments for per-island position projection.
 *
 * @par Ownership
 * - island, constraints, bodies: borrowed, read-only.
 * - arena: borrowed, allocations made for output arrays.
 * - result: caller-owned output struct.
 * - shared_pos_deltas / shared_vel_deltas: optional pre-allocated arrays
 *   (sized to body_count) to avoid per-island arena allocation.  If NULL,
 *   the function allocates from the arena.
 */
typedef struct phys_position_projection_args {
    const struct phys_island *island;    /**< Island to project. */
    const struct phys_constraint *constraints; /**< Global constraint array. */
    const struct phys_body *bodies;      /**< Global body array. */
    uint32_t body_count;                 /**< Total body count (for output array sizing). */
    float dt;                            /**< Timestep in seconds. */
    float slop;                          /**< Penetration slop (no correction below this). */
    struct phys_frame_arena *arena;      /**< Arena for output allocations. */
    phys_position_projection_result_t *result; /**< Output. */
    phys_vec3_t *shared_pos_deltas;      /**< Optional shared position delta array. */
    struct phys_velocity *shared_vel_deltas; /**< Optional shared velocity delta array. */
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
